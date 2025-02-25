PRAGMA warning('disable', '4510');
PRAGMA config.flags('OptimizerFlags', 'UnorderedOverSortImproved');

$l = ListMap(ListFromRange(1, 10), ($x) -> (AsStruct($x + 1 AS x, $x AS y)));
$top_by_neg_x = ListTake(ListSort($l, ($row) -> (-$row.x)), 3);
$y = ListMap($top_by_neg_x, ($row) -> ($row.y));

SELECT
    ListSum(YQL::Unordered($y))
;
