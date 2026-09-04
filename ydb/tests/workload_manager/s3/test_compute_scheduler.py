from ydb.tests.olap.load.lib import tpch
from ydb.tests.workload_manager.common.workload_manager import ResourcePool
from ydb.tests.workload_manager.s3.lib.base import (
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


class WorkloadManagerS3ComputeSchedulerP1(WorkloadManagerS3ComputeScheduler):
    """S3-scheduled variant of the P1 pool configuration
    (single 100 % per-node CPU pool), mirroring
    WorkloadManagerComputeSchedulerP1."""

    @classmethod
    def get_resource_pools(cls) -> list[ResourcePool]:
        return [
            ResourcePool('test_pool_100', ['testuser100'], total_cpu_limit_percent_per_node=100, resource_weight=4),
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


class TestWorkloadManagerS3TpchComputeSchedulerP1S10(
        S3WorkloadManagerFunctionalBase,
        WorkloadManagerS3TpchBase,
        WorkloadManagerS3ComputeSchedulerP1):
    tables_size = tpch.TpchParallelS1T10.tables_size
    scale = tpch.TpchParallelS1T10.scale
    timeout = tpch.TpchParallelS1T10.timeout * len(WorkloadManagerS3ComputeSchedulerP1.get_resource_pools())
    iterations = tpch.TpchParallelS1T10.iterations
