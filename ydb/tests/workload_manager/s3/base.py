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


# User pool sized so TotalCpuLimit is large enough for meaningful integer
# cap differentiation. Default KikiMR gives User pool 3 threads, which makes
# 30/40/50% pool caps all collapse to 1 (via `max(1, pct * total / 100)` with
# integer division in kqp_compute_scheduler_service.cpp). At 16 threads the
# same percentages resolve to 4/6/8 CPUs — distinct and non-trivial. Other
# pools kept at their defaults from resources/default_yaml.yml.
_S3_ACTOR_SYSTEM_CONFIG = {
    "executor": [
        {"type": "BASIC", "name": "System", "threads": 2,  "spin_threshold": 0},
        {"type": "BASIC", "name": "User",   "threads": 16, "spin_threshold": 0},
        {"type": "BASIC", "name": "Batch",  "threads": 2,  "spin_threshold": 0},
        {"type": "IO",    "name": "IO",     "threads": 1,  "time_per_mailbox_micro_secs": 100},
        {"type": "BASIC", "name": "IC",     "threads": 1,  "spin_threshold": 10, "time_per_mailbox_micro_secs": 100},
    ],
    "scheduler": {"resolution": 1024, "spin_threshold": 0},
}


class S3WorkloadManagerFunctionalBase(FunctionalTestBase):
    """Hermetic KiKiMR setup for S3 scheduling tests.

    Enables the feature flags, query-service config, and actor-system pool
    sizing the S3 CPU scheduler needs; then hands off to the WM chain's
    setup_class (LoadSuiteBase -> perform_verification -> do_setup_class ->
    benchmark_setup -> pools/EDS).

    MRO note: place this class *first* in the concrete Test class's bases so
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
            },
            actor_system_config=_S3_ACTOR_SYSTEM_CONFIG,
        )
        super().setup_class()


class WorkloadManagerS3ComputeScheduler(WorkloadManagerComputeScheduler):
    """
    Compute-scheduler variant that polls per-pool S3 CPU-scheduling sensors,
    ramps thread count until a capped pool saturates, and asserts the cap
    was not exceeded.

    Assertion pair:
      * Saturation witness: `Waiting > 0` sustained for at least
        `saturation_min_polls` consecutive 1s polls in some capped pool
        during an attempt. Without it, `usage/limit <= 1.0` proves nothing.
      * Cap effectiveness: p95 of per-poll `Usage/Limit` per capped pool,
        exposed as `usage_over_limit_p95_<pool>` KeyMeasurement, target <= 1.0.

    Ramp: threads doubles across attempts from `threads_min` up to
    `threads_max` until saturation is observed.

    Sensor names track `schedulerPool` subgroup in
    ydb/core/kqp/runtime/scheduler/tree/dynamic.cpp.
    """

    threads_min: int = 4
    threads_max: int = 64
    saturation_min_polls: int = 5
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
                        scale=self.scale,
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
    """Mixin for perf-lab S3 scheduling tests: TPC-H orders scanned via
    EXTERNAL DATA SOURCE with inline schema.

    Uses the same public Yandex Cloud bucket that
    ydb/tests/olap/s3_import/large/test_large_import.py reads from. Only the
    EXTERNAL DATA SOURCE is created (no EXTERNAL TABLE); every query carries
    its own SCHEMA/FORMAT/FILE_PATTERN. Mirrors the SQL used in manual load
    tests: `SELECT SOME(...)` over all columns with `MaxTasksPerStage` cranked
    up to force per-file parallelism against S3.
    """

    workload_type = WorkloadType.EXTERNAL
    iterations: int = tpch.TpchParallelBase.iterations
    max_tasks_per_stage: int = 300

    @classmethod
    def _get_source_path(cls) -> str:
        # EDS lives at `<_tables_path>/<get_path()>` (typically
        # `olap_yatests/tpch_s3/sN`). This is the exact path that
        # WorkloadRunner.wait_ydb_alive(self.db_path) describes when it
        # health-checks the workload's `--path` before running (see
        # ydb/tests/olap/lib/ydb_cli.py:266). By placing the EDS itself at
        # that path, describe_path returns a scheme entry (not "Path not
        # found") and the workload proceeds. No child suffix (/src etc.)
        # because we want the EDS name to *be* that path.
        return YdbCluster.get_tables_path(cls.get_path())

    @classmethod
    def _get_scan_query(cls, pragma_prefix: str = '') -> str:
        # SOME() over every column forces the S3 read actor to actually
        # decode all columns — unlike COUNT(*), which can be answered from
        # parquet row-group metadata alone without decoding rows and would
        # not exercise the CPU throttle path.
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
        pragmas = f'''
            PRAGMA ydb.MaxTasksPerStage = "{cls.max_tasks_per_stage}";
            PRAGMA ydb.OverridePlanner = @@ [
                {{ "tx": 0, "stage": 0, "tasks": {cls.max_tasks_per_stage} }}
            ] @@;
        '''
        return [cls._get_scan_query(pragma_prefix=pragmas)]

    @classmethod
    def get_path(cls) -> str:
        return get_external_param(
            f'table-path-{cls.suite()}',
            f'tpch_s3/s{cls.scale}'.replace('.', '_'),
        )

    @classmethod
    def benchmark_setup(cls) -> None:
        # `enable_s3_scheduling` and `enable_external_data_sources` are turned
        # on by S3WorkloadManagerFunctionalBase.setup_class before we get here,
        # so no preflight witness is needed.
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
