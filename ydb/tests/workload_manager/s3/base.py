from __future__ import annotations

import allure
import pytest
import time
import ydb

from threading import Thread

from ydb.tests.functional.tpc.lib.conftest import FunctionalTestBase
from ydb.tests.olap.load.lib import tpch
from ydb.tests.olap.load.lib.conftest import LoadSuiteBase
from ydb.tests.olap.lib.utils import get_external_param
from ydb.tests.olap.lib.ydb_cli import YdbCliHelper, WorkloadType
from ydb.tests.olap.lib.ydb_cluster import YdbCluster
from ydb.tests.workload_manager.common.workload_manager import (
    ResourcePool,
    WorkloadManagerComputeScheduler,
)


class S3WorkloadManagerFunctionalBase(FunctionalTestBase):
    """KiKiMR setup for S3 scheduling tests.

    NOTE: place this class *first* in the concrete Test class's bases so
    its setup_class wins over LoadSuiteBase.setup_class, which sits deeper in
    the WM chain and would otherwise be picked by method resolution.
    """

    @classmethod
    def setup_class(cls) -> None:
        cls.setup_cluster_ext(
            extra_feature_flags=[
                "enable_s3_scheduling",
                "enable_external_data_sources",
            ],
            query_service_config={
                "available_external_data_sources": ["ObjectStorage"],
                # Per-task S3 read buffer defaults to 200 MiB
                # Under concurrent runners that overshoots the
                # KiKiMR's memory quota. Use only 16MB
                "s3": {
                    "data_inflight": 16 * 1024 * 1024,
                },
            },
        )
        super().setup_class()


class WorkloadManagerS3ComputeScheduler(WorkloadManagerComputeScheduler):
    """
    Compute-scheduler variant that polls per-pool S3 CPU-scheduling sensors,
    ramps thread count until a capped pool saturates, and asserts the cap
    was not exceeded.
    """

    # ramp attempt from min to max
    threads_min: int = 2
    threads_max: int = 16

    # `Waiting > 0` has to be during at least this
    # amount of consequtive polls
    saturation_min_polls: int = 10

    # how much to ramp if test failed
    ramp_max_attempts: int = 3

    _saturated: bool = False
    _final_threads: int = 0
    _attempt_starts: list[int] = []

    @classmethod
    def get_key_measurements(cls) -> tuple[list[LoadSuiteBase.KeyMeasurement], str]:
        kms, doc = super().get_key_measurements()
        for p in cls.get_resource_pools():
            if p.params.get('total_cpu_limit_percent_per_node'):
                kms.append(LoadSuiteBase.KeyMeasurement(
                    f'usage_over_limit_p95_{p.name}',
                    f'Usage/Limit p95 {p.name}',
                    [
                        LoadSuiteBase.KeyMeasurement.Interval('#ccffcc', 1.05),
                        LoadSuiteBase.KeyMeasurement.Interval('#ffcccc'),
                    ],
                    f'p95 of per-poll Usage/Limit for capped pool <b>{p.name}</b>. Target &le; 1.0.'
                ))
        doc += '''

        <p>Parameter <b>usage_over_limit_p95</b> is the p95 of per-poll
        <code>Usage / Limit</code> for a pool with an explicit
        <code>total_cpu_limit_percent_per_node</code>. Target &le; 1.0. Values
        &gt; 1.05 mean the cap was exceeded &mdash; S3 CPU throttling did not
        hold the pool under its configured cap.</p>'''
        return kms, doc

    @classmethod
    def check_signals(cls) -> str:
        metrics_request = {}
        for pool in cls.get_resource_pools():
            metrics_request.update({
                f'{pool.name} satisfaction':            {'schedulerPool': pool.name, 'sensor': 'Satisfaction'},
                f'{pool.name} adjusted satisfaction d': {'schedulerPool': pool.name, 'sensor': 'AdjustedSatisfaction'},
                f'{pool.name} usage d':                 {'schedulerPool': pool.name, 'sensor': 'Usage'},
                f'{pool.name} throttle d':              {'schedulerPool': pool.name, 'sensor': 'Throttle'},
                f'{pool.name} limit':                   {'schedulerPool': pool.name, 'sensor': 'Limit'},
                f'{pool.name} fair_share':              {'schedulerPool': pool.name, 'sensor': 'FairShare'},
                f'{pool.name} waiting':                 {'schedulerPool': pool.name, 'sensor': 'Waiting'},
            })
        metrics = YdbCluster.get_metrics(db_only=True, counters='kqp', metrics=metrics_request)
        agg_sum: dict[str, float] = {}
        agg_count: dict[str, int] = {}
        for _slot, values in metrics.items():
            for k, v in values.items():
                if not k.endswith('satisfaction') or v >= 0.:
                    agg_sum.setdefault(k, 0.)
                    agg_count.setdefault(k, 0)
                    agg_sum[k] += v
                    agg_count[k] += 1
                cls.metrics_keys.add(k)
        for k in agg_sum.keys():
            if agg_count[k] > 0:
                agg_sum[k] /= agg_count[k]
            if k.find('satisfaction') >= 0:
                agg_sum[k] /= 1.e6
        cls.metrics.append((time.time(), agg_sum))
        return ''

    @classmethod
    def _capped_pools(cls) -> list[ResourcePool]:
        return [p for p in cls.get_resource_pools()
                if p.params.get('total_cpu_limit_percent_per_node')]

    @classmethod
    def _attempt_saturated(cls, start_idx: int) -> bool:
        """True if any capped pool saw `Waiting > 0` for at least
        `saturation_min_polls` consecutive 1s samples since `start_idx`."""
        for pool in cls._capped_pools():
            consecutive = 0
            for _t, m in cls.metrics[start_idx:]:
                if m.get(f'{pool.name} waiting', 0) > 0:
                    consecutive += 1
                    if consecutive >= cls.saturation_min_polls:
                        return True
                else:
                    consecutive = 0
        return False

    @classmethod
    def after_workload(cls, result: YdbCliHelper.WorkloadRunResult):
        super().after_workload(result)

        if not cls.metrics:
            return

        for pool in cls._capped_pools():
            ratios: list[float] = []
            for _t, m in cls.metrics:
                usage = m.get(f'{pool.name} usage d', 0.)
                limit = m.get(f'{pool.name} limit', 0.)
                if limit > 0:
                    ratios.append(usage / limit)
            if ratios:
                ratios.sort()
                p95 = ratios[min(int(len(ratios) * 0.95), len(ratios) - 1)]
                result.add_stat('test', f'usage_over_limit_p95_{pool.name}', p95)

        if cls._capped_pools() and not cls._saturated:
            pytest.fail(
                f'Workload never saturated any capped pool: needed >= '
                f'{cls.saturation_min_polls} consecutive polls with Waiting > 0. '
                f'Final threads: {cls._final_threads}, max: {cls.threads_max}. '
                f'CPU-limit effectiveness could not be verified.'
            )

    def test(self):
        check_thread = Thread(target=self.check_signals_thread)
        self.stop_checking.clear()
        check_thread.start()
        overall_result = YdbCliHelper.WorkloadRunResult()
        all_results: dict = {}
        self.__class__._saturated = False
        self.__class__._attempt_starts = []
        threads = max(self.threads_min, self.threads or self.threads_min)
        try:
            qparams = self._get_query_settings()
            self.save_nodes_state()
            self.before_workload(overall_result)
            for attempt in range(self.ramp_max_attempts):
                self.__class__._attempt_starts.append(len(self.__class__.metrics))
                self.__class__._final_threads = threads
                if self.workload_type is not None:
                    results = YdbCliHelper.workload_run(
                        path=self.get_path(),
                        query_names=self.get_query_list(),
                        iterations=qparams.iterations,
                        workload_type=self.workload_type,
                        timeout=qparams.timeout,
                        check_canonical=self.check_canonical,
                        query_syntax=self.query_syntax,
                        scale=None,
                        query_prefix=qparams.query_prefix,
                        external_path=self.get_external_path(),
                        threads=threads,
                        users=self.get_users(),
                    )
                    all_results = results
                    for query, result in results.items():
                        try:
                            with allure.step(f'{query} (attempt {attempt + 1}, threads={threads})'):
                                self.process_query_result(result, query, True)
                        except BaseException:
                            pass
                else:
                    results = {}
                if self._attempt_saturated(self.__class__._attempt_starts[-1]):
                    self.__class__._saturated = True
                    break
                if threads >= self.threads_max:
                    break
                threads = min(threads * 2, self.threads_max)
            self.after_workload(overall_result)
        finally:
            self.stop_checking.set()
            check_thread.join()
        if len(all_results) > 0:
            overall_result.merge(*all_results.values())
        overall_result.iterations.clear()
        self.process_query_result(overall_result, 'test', True)
        if len(self.signal_errors) > 0:
            errors = '\n'.join([f'{d}: {e}' for d, e in self.signal_errors])
            pytest.fail(f'Errors while execute: {errors}')


class WorkloadManagerS3TpchBase:
    workload_type = WorkloadType.EXTERNAL
    iterations: int = tpch.TpchParallelBase.iterations

    @classmethod
    def _get_source_path(cls) -> str:
        return YdbCluster.get_tables_path(cls.get_path())

    @classmethod
    def _get_scan_query(cls, pragma_prefix: str = '') -> str:
        return f'''{pragma_prefix}
            SELECT
                SOME(o_clerk),
                SOME(o_comment),
                SOME(o_custkey),
                SOME(o_orderdate),
                SOME(o_orderkey),
                SOME(o_orderpriority),
                SOME(o_orderstatus),
                SOME(o_shippriority),
                SOME(o_totalprice)
            FROM `{cls._get_source_path()}`.`h/s{cls.scale}/parquet/orders/`
            WITH (
                FORMAT = "parquet",
                SCHEMA = (
                    o_orderkey      Int64,
                    o_custkey       Int64,
                    o_orderstatus   Utf8,
                    o_totalprice    Double,
                    o_orderdate     Date32,
                    o_orderpriority Utf8,
                    o_clerk         Utf8,
                    o_shippriority  Int32,
                    o_comment       Utf8
                ),
                FILE_PATTERN = "*.parquet"
            );
        '''

    @classmethod
    def get_query_list(cls) -> list[str]:
        return [cls._get_scan_query()]

    @classmethod
    def get_path(cls) -> str:
        return get_external_param(
            f'table-path-{cls.suite()}',
            f'tpch_s3/s{cls.scale}'.replace('.', '_'),
        )

    @classmethod
    def benchmark_setup(cls) -> None:
        endpoint = get_external_param('s3-endpoint', 'https://storage.yandexcloud.net')
        bucket = get_external_param('s3-bucket', 'tpc')
        sessions_pool = ydb.QuerySessionPool(YdbCluster.get_ydb_driver())
        sessions_pool.execute_with_retries(f'''
            CREATE OR REPLACE EXTERNAL DATA SOURCE `{cls._get_source_path()}` WITH (
                SOURCE_TYPE="ObjectStorage",
                LOCATION="{endpoint}/{bucket}/",
                AUTH_METHOD="NONE"
            );
        ''')
