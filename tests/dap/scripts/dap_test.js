// DAP test script — exercises all debug API features
// Used by dap-test-bridge for verification

var x = 10;
var y = 20;

function add(a, b) {
    var result = a + b;        // line 7: breakpoint target
    return result;
}

function main() {
    var sum = add(x, y);       // line 12: breakpoint target
    var product = x * y;       // line 13

    // Conditional breakpoint: only break when sum > 25
    if (sum > 0) {             // line 16: conditional bp target
        var msg = "sum=" + sum;
    }

    // Exception test
    try {
        throw new Error("test exception");  // line 22: exception bp target
    } catch (e) {
        var caught = e.message;
    }

    // debugger statement
    debugger;                  // line 28

    var final = sum + product;
}

main();
