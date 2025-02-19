#pragma once

#include <Analyzer/IQueryTreeNode.h>

namespace DB
{

struct ValidationParams
{
    bool group_by_use_nulls;
};

/** Validate aggregates in query node.
  *
  * 1. Check that there are no aggregate functions and GROUPING function in JOIN TREE, WHERE, PREWHERE, in another aggregate functions.
  * 2. Check that there are no window functions in JOIN TREE, WHERE, PREWHERE, HAVING, WINDOW, inside another aggregate function,
  * inside window function arguments, inside window function window definition.
  * 3. Check that there are no columns that are not specified in GROUP BY keys in HAVING, ORDER BY, PROJECTION.
  * 4. Check that there are no GROUPING functions that have arguments that are not specified in GROUP BY keys in HAVING, ORDER BY,
  * PROJECTION.
  * 5. Throws exception if there is GROUPING SETS or ROLLUP or CUBE or WITH TOTALS without aggregation.
  */
void validateAggregates(const QueryTreeNodePtr & query_node, ValidationParams params);

/** Assert that there are no function nodes with specified function name in node children.
  * Do not visit subqueries.
  */
void assertNoFunctionNodes(const QueryTreeNodePtr & node,
    std::string_view function_name,
    int exception_code,
    std::string_view exception_function_name,
    std::string_view exception_place_message);

}
