from yql.essentials.providers.common.proto.gateways_config_pb2 import EGenericDataSourceKind
from ydb.library.yql.providers.generic.connector.tests.utils.comparator import assert_data_outs_equal
from ydb.library.yql.providers.generic.connector.tests.utils.log import make_logger
from ydb.library.yql.providers.generic.connector.tests.utils.settings import Settings
from ydb.library.yql.providers.generic.connector.tests.utils.run.parent import Runner

from ydb.library.yql.providers.generic.connector.tests.utils.clients.postgresql import Client as PostgreSQLClient
import ydb.library.yql.providers.generic.connector.tests.utils.scenario.postgresql as postgresql_scenario

from test_case import TestCase

LOGGER = make_logger(__name__)


def join(
    test_name: str,
    test_case: TestCase,
    settings: Settings,
    runner: Runner,
    postgresql_client: PostgreSQLClient,
):
    # prepare tables
    for data_source in test_case.data_sources:
        match data_source.kind:
            case EGenericDataSourceKind.CLICKHOUSE:
                # do nothing as tables are initialized via init scripts
                continue
            case EGenericDataSourceKind.POSTGRESQL:
                postgresql_scenario.prepare_table(
                    test_name=test_name,
                    client=postgresql_client,
                    database=data_source.database,
                    table_name=data_source.table.name,
                    data_in=data_source.table.data_in,
                    schema=data_source.table.schema,
                )
            case _:
                raise Exception(f'invalid data source: {data_source.kind}')

    # run join
    yql_script = test_case.make_sql(settings)

    result = runner.run(test_name=test_name, script=yql_script, generic_settings=test_case.generic_settings)

    assert result.returncode == 0, result.output

    assert_data_outs_equal(test_case.data_out, result.data_out_with_types), (
        test_case.data_out,
        result.data_out_with_types,
    )
