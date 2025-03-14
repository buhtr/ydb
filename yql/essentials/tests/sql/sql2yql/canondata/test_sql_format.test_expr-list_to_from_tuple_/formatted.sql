SELECT
    ListFromTuple(NULL),
    ListFromTuple(()),
    ListFromTuple((1, 2)),
    ListFromTuple(just((3, 4))),
    ListFromTuple(Nothing(Tuple<Int32, Int32>?)),
    ListToTuple(NULL, 10),
    ListToTuple([], 0),
    ListToTuple(ListCreate(Int32), 0),
    ListToTuple([1, 2], 2),
    ListToTuple(just([3, 4]), 2)
;
