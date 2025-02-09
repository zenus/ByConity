#include <Parsers/ParserOptimizeQuery.h>

#include <IO/WriteBufferFromOStream.h>
#include <Parsers/ParserQueryWithOutput.h>
#include <Parsers/formatAST.h>
#include <Parsers/parseQuery.h>

#include <memory>
#include <string_view>
#include <IO/WriteBufferFromOStream.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/ParserQueryWithOutput.h>
#include <Parsers/formatAST.h>
#include <Parsers/parseQuery.h>

#include <Parsers/ParserDropQuery.h>
#include <gtest/gtest.h>
#include "Parsers/ParserPartToolkitQuery.h"

namespace
{
using namespace DB;
using namespace std::literals;
}

struct ParserTestCase
{
    std::shared_ptr<IParser> parser;
    const std::string_view input_text;
    const char * expected_ast = nullptr;
};

std::ostream & operator<<(std::ostream & ostr, const ParserTestCase & test_case)
{
    return ostr << "parser: " << test_case.parser->getName() << ", input: " << test_case.input_text;
}

class ParserTest : public ::testing::TestWithParam<ParserTestCase>
{
};

TEST_P(ParserTest, parseQuery)
{
    const auto & [parser, input_text, expected_ast] = GetParam();

    ASSERT_NE(nullptr, parser);

    if (expected_ast)
    {
        ASTPtr ast;
        ASSERT_NO_THROW(ast = parseQuery(*parser, input_text.begin(), input_text.end(), 0, 0));
        EXPECT_EQ(expected_ast, serializeAST(*ast->clone(), false));
    }
    else
    {
        ASSERT_THROW(parseQuery(*parser, input_text.begin(), input_text.end(), 0, 0), DB::Exception);
    }
}


INSTANTIATE_TEST_SUITE_P(
    ParserOptimizeQuery,
    ParserTest,
    ::testing::Values(
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('a, b')",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('a, b')"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]')",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]')"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') EXCEPT b",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') EXCEPT b"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') EXCEPT (a, b)",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') EXCEPT (a, b)"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY a, b, c",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY a, b, c"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY *",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY *"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * EXCEPT a",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * EXCEPT a"},
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * EXCEPT (a, b)",
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * EXCEPT (a, b)"}));

INSTANTIATE_TEST_SUITE_P(
    ParserOptimizeQuery_FAIL,
    ParserTest,
    ::testing::Values(
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY",
        },
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') APPLY(x)",
        },
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY COLUMNS('[a]') REPLACE(y)",
        },
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * APPLY(x)",
        },
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY * REPLACE(y)",
        },
        ParserTestCase{
            std::make_shared<ParserOptimizeQuery>(),
            "OPTIMIZE TABLE table_name DEDUPLICATE BY db.a, db.b, db.c",
        }));

INSTANTIATE_TEST_SUITE_P(
    ParserPartToolkitQuery,
    ParserTest,
    ::testing::Values(
        ParserTestCase{
            std::make_shared<ParserPartToolkitQuery>(nullptr),
            "load CSV file 'test.csv' as table default.tmp (p_date Date, id Int32, kv Map(String, Int32)) ENGINE=CloudMergeTree(my_db, "
            "tmp) PARTITION BY (p_date) ORDER BY (id) location 'hdfs://test/path' settings "
            "max_insert_block_size=0,min_insert_block_size_rows=0,min_insert_block_size_bytes=0",
            ""},
        ParserTestCase{
            std::make_shared<ParserPartToolkitQuery>(nullptr),
            "LOAD Parquet file "
            "'hdfs://housebackend/user/bytehouse/dataexpress/execution/3634d56f-3970-4390-9dfd-36ac8c1a7959/pw/2023-07-21T07-53-48/parquet/"
            "part-00000-8c5e9662-f50a-49ed-a0cb-6632cf5ba55f-c000.snappy.parquet' AS TABLE `1193474528.ericcys`.`npc_cases` (`year` Int64, "
            "`npc` String, `offence` String, `case_no` Int64) PRIMARY KEY year ORDER BY year LOCATION "
            "'#user#bytehouse#dataexpress#execution#3634d56f-3970-4390-9dfd-36ac8c1a7959#pw#2023-07-21T07-53-48#parts#0' SETTINGS cnch=1, "
            "max_partitions_per_insert_block=10000, max_insert_block_size=1048576, "
            "s3_output_config='/opt/spark/work-dir/cnch-secrets/s3-creds.ini', hdfs_nnproxy='nnproxy'",
            "",
        },
        ParserTestCase{
            std::make_shared<ParserPartToolkitQuery>(nullptr),
            "clean s3 task all '<task_id>' settings s3_config='./s3.conf'",
            "",
        },
        ParserTestCase{
            std::make_shared<ParserPartToolkitQuery>(nullptr),
            "LOAD PARQUET file './part-00002-4673fab3-3d19-4e84-b0ee-ec498add5c15-c000.snappy.parquet' AS TABLE `dataexpress`.`uba_2aug` "
            "(`app_id` UInt32, `app_name` String, `device_id` String, `web_id` String, `hash_uid` UInt64, `server_time` UInt64, `time` "
            "UInt64, `event` String, `stat_standard_id` String, `event_date` Date, `ab_version` Array(Int32), `string_params` Map(String, "
            "String), `int_params` Map(String, Int64), `float_params` Map(String, Float64), `string_profiles` Map(String, String), "
            "`int_profiles` Map(String, Int64), `float_profiles` Map(String, Float64), `user_id` String, `ssid` String, `content` String, "
            "`string_array_profiles` Map(String, Array(String)), `string_array_params` Map(String, Array(String)), `string_item_profiles` "
            "Map(String, Array(String)), `float_item_profiles` Map(String, Array(Float32)), `int_item_profiles` Map(String, Array(Int64))) "
            "PARTITION BY (`app_id`, `event_date`) PRIMARY KEY (`event`, `hash_uid`, `time`) ORDER BY (`event`, `hash_uid`, `time`) "
            "CLUSTER BY `hash_uid` INTO 40 BUCKETS LOCATION 'hdfs:///home/byte_dataplatform_olap_engines/user/fengkaiyu.hi/test' SETTINGS "
            "cnch=1, max_partitions_per_insert_block=10000, max_insert_block_size=1048576",
            "",
        }));



    INSTANTIATE_TEST_SUITE_P(
        ParserCreateCatalogQuery,
        ParserTest,
        ::testing::Values(
            ParserTestCase{
                std::make_shared<ParserCreateQuery>(),
                "CREATE EXTERNAL CATALOG mock_catalog PROPERTIES type = 'Mock'",
                "CREATE EXTERNAL CATALOG mock_catalog PROPERTIES type = 'Mock'",
            },
            ParserTestCase{
                std::make_shared<ParserCreateQuery>(),
                "CREATE EXTERNAL CATALOG hive_catalog PROPERTIES type = 'hive', hive.metastore.uri = 'thrift://localhost:9183'"
                "CREATE EXTERNAL CATALOG hive_catalog PROPERTIES type = 'hive', hive.metastore.uri = 'thrift://localhost:9183'"}));
    
    INSTANTIATE_TEST_SUITE_P(
        ParserDropCatalogQuery,
        ParserTest,
        ::testing::Values(ParserTestCase{
            std::make_shared<ParserDropQuery>(),
            "DROP EXTERNAL CATALOG mock_catalog",
            "DROP EXTERNAL CATALOG mock_catalog",
        }));
    
    INSTANTIATE_TEST_SUITE_P(ParserCreateCatalogQuery_FAIL, ParserTest, ::testing::Values(
        ParserTestCase{
           std::make_shared<ParserCreateQuery>(),
           "CREATE DATABASE mock_catalog.db" ,
        }
));
