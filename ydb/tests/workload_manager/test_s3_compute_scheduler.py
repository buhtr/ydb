from ydb.tests.olap.load.lib import tpch
from ydb.tests.workload_manager.common.workload_manager import ResourcePool
from ydb.tests.workload_manager.s3.base import (
    S3WorkloadManagerFunctionalBase,
    WorkloadManagerS3ComputeScheduler,
    WorkloadManagerS3TpchBase,
)


class WorkloadManagerS3ComputeSchedulerP3(WorkloadManagerS3ComputeScheduler):
    """S3-scheduled variant of the P3 pool configuration
    (three pools, 30/40/50 % per-node CPU cap, equal weight),
    mirroring WorkloadManagerComputeSchedulerP3."""

    @classmethod
    def get_resource_pools(cls) -> list[ResourcePool]:
        return [
            ResourcePool('test_pool_30', ['testuser30'], total_cpu_limit_percent_per_node=30, resource_weight=4),
            ResourcePool('test_pool_40', ['testuser40'], total_cpu_limit_percent_per_node=40, resource_weight=4),
            ResourcePool('test_pool_50', ['testuser50'], total_cpu_limit_percent_per_node=50, resource_weight=4),
        ]


class WorkloadManagerS3Cap10Percent(WorkloadManagerS3ComputeScheduler):
    """Single pool capped at 10 % per-node CPU. On the 16-worker hermetic
    User pool that resolves to a 1-CPU budget (max(1, 10*16/100)=1), so
    even one query's decode work overshoots and the throttle path engages.
    Deliberately narrow so a network-bound S3 workload can still exercise
    the cap."""

    @classmethod
    def get_resource_pools(cls) -> list[ResourcePool]:
        return [
            ResourcePool('test_pool_10', ['testuser10'], total_cpu_limit_percent_per_node=10, resource_weight=4),
        ]


# NOTE on MRO: S3WorkloadManagerFunctionalBase must come first so its
# setup_class wins over LoadSuiteBase.setup_class inherited via the WM
# chain. It calls setup_cluster with the S3 flags then chains into the WM
# setup via super().


class TestWorkloadManagerS3TpchComputeSchedulerS100(
        S3WorkloadManagerFunctionalBase,
        WorkloadManagerS3TpchBase,
        WorkloadManagerS3ComputeSchedulerP3):
    tables_size = tpch.TestTpch100.tables_size
    scale = tpch.TestTpch100.scale
    timeout = tpch.TestTpch100.timeout * len(WorkloadManagerS3ComputeSchedulerP3.get_resource_pools())


class TestWorkloadManagerS3TpchCap10PercentScale10(
        S3WorkloadManagerFunctionalBase,
        WorkloadManagerS3TpchBase,
        WorkloadManagerS3Cap10Percent):
    # scale=10: orders ~15M rows / ~3 GB parquet, big enough that decode CPU
    # is non-trivial while staying within a MEDIUM-sized test envelope.
    scale = 10
    # Explicit values sized for scale=10 (not inherited from
    # TpchParallelS1T10 which targets scale=1).
    iterations = 3
    timeout = 900.0
