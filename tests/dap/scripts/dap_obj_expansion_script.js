"use strict";
var obj = { name: "test", value: 42, nested: { x: 1 } };
var arr = [10, 20, 30];

function main() {
    var localObj = { a: 1, b: "hello" };
    var localArr = [100, 200];
    debugger;                    // line 8
    return obj.name + arr[0];
}

main();
