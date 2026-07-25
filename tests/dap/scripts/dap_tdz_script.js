"use strict";
function fn(x) {
    let sum = x + 1;  // line 3: let initialized
    debugger;          // line 4: stop here, sum is initialized
    return sum;
}
fn(10);
