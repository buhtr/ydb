from ydb.tests.workload_manager.common.workload_manager import ResourcePool
from ydb.tests.workload_manager.s3.base import (
    S3WorkloadManagerFunctionalBase,
    WorkloadManagerS3ComputeScheduler,
    WorkloadManagerS3TpchBase,
)


class WorkloadManagerS3Cap10Percent(WorkloadManagerS3ComputeScheduler):
    """Single pool capped at 10 % per-node CPU. On the default 3-worker
    hermetic User pool that resolves to a 1-CPU budget (max(1, 10*3/100)=1),
    so even one query's decode work overshoots and the throttle path
    engages. Deliberately narrow so a network-bound S3 workload can still
    exercise the cap."""

    @classmethod
    def get_resource_pools(cls) -> list[ResourcePool]:
        return [
            ResourcePool('test_pool_10', ['testuser10'], total_cpu_limit_percent_per_node=10, resource_weight=4),
        ]


# NOTE on MRO: S3WorkloadManagerFunctionalBase must come first so its
# setup_class wins over LoadSuiteBase.setup_class inherited via the WM
# chain. It calls setup_cluster with the S3 flags then chains into the WM
# setup via super().


class TestWorkloadManagerS3TpchCap10PercentScale1(
        S3WorkloadManagerFunctionalBase,
        WorkloadManagerS3TpchBase,
        WorkloadManagerS3Cap10Percent):
    # scale=1: orders ~150k rows / ~300 MB parquet. Small enough that even
    # at 1 CPU (10% of 10-worker User pool) each iteration completes in
    # seconds, so the whole ramp fits in the MEDIUM-size 600 s envelope.
    # Cap effectiveness is still exercised because 2+ concurrent runners
    # each try to decode, but the pool only admits 1 at a time.
    scale = 1
    iterations = 1
    timeout = 300.0
