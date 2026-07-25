"use strict";
var g = 42;

function strictFn(x, y) {
    let sum = x + y;
    const product = x * y;
    var legacy = sum + product;
    var z = 99;                   // line 8
    return sum + product;         // line 9
}

strictFn(10, 20);                 // line 12
