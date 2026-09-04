from ydb.tests.workload_manager.common.workload_manager import ResourcePool
from ydb.tests.workload_manager.s3.base import (
    S3WorkloadManagerFunctionalBase,
    WorkloadManagerS3ComputeScheduler,
    WorkloadManagerS3TpchBase,
)


class WorkloadManagerS3Cap10Percent(WorkloadManagerS3ComputeScheduler):
    @classmethod
    def get_resource_pools(cls) -> list[ResourcePool]:
        return [
            ResourcePool('test_pool_10', ['testuser10'], total_cpu_limit_percent_per_node=10, resource_weight=4),
        ]


class TestWorkloadManagerS3TpchCap10PercentScale1(
        S3WorkloadManagerFunctionalBase,
        WorkloadManagerS3TpchBase,
        WorkloadManagerS3Cap10Percent):
    scale = 1
    iterations = 1
    timeout = 300.0
