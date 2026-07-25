"use strict";
function testFn(x, y) {
    let sum = x + y;       // line 3
    const product = x * y; // line 4
    var legacy = sum + product;  // line 5
    var z = 99;            // line 6
    return legacy + z;     // line 7
}
testFn(10, 20);            // line 9
