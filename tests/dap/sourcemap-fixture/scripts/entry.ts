// dap_sourcemap_test.ts — TypeScript test script for SourceMap verification
// This file is compiled to dap_sourcemap_test.js with inline source map

var x = 10;
var y = 20;

function add(a: number, b: number): number {
    var result = a + b;        // line 7: breakpoint target
    return result;
}

function main(): void {
    var sum = add(x, y);       // line 12: breakpoint target
    var product = x * y;       // line 13

    // Conditional breakpoint: only break when sum > 25
    if (sum > 0) {             // line 16: conditional bp target
        var msg = "sum=" + sum;
    }

    // debugger statement
    debugger;                  // line 20: debugger stmt target

    var final_val = sum + product;
    console.log("final=" + final_val);
}

main();
