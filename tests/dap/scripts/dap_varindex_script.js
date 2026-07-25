"use strict";
function testFn(x, y) {
    var legacy = x + y;    // line 3
    var z = 99;            // line 4
    return legacy + z;     // line 5
}
testFn(10, 20);            // line 7
