"use strict";
// dap_sourcemap_test.ts — TypeScript test script for SourceMap verification
// This file is compiled to dap_sourcemap_test.js with inline source map
var x = 10;
var y = 20;
function add(a, b) {
    var result = a + b; // line 7: breakpoint target
    return result;
}
function main() {
    var sum = add(x, y); // line 12: breakpoint target
    var product = x * y; // line 13
    // Conditional breakpoint: only break when sum > 25
    if (sum > 0) { // line 16: conditional bp target
        var msg = "sum=" + sum;
    }
    // debugger statement
    debugger; // line 20: debugger stmt target
    var final_val = sum + product;
    console.log("final=" + final_val);
}
main();
//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoiZGFwX3NvdXJjZW1hcF90ZXN0LmpzIiwic291cmNlUm9vdCI6IiIsInNvdXJjZXMiOlsiZGFwX3NvdXJjZW1hcF90ZXN0LnRzIl0sIm5hbWVzIjpbXSwibWFwcGluZ3MiOiI7QUFBQSw0RUFBNEU7QUFDNUUsd0VBQXdFO0FBRXhFLElBQUksQ0FBQyxHQUFHLEVBQUUsQ0FBQztBQUNYLElBQUksQ0FBQyxHQUFHLEVBQUUsQ0FBQztBQUVYLFNBQVMsR0FBRyxDQUFDLENBQVMsRUFBRSxDQUFTO0lBQzdCLElBQUksTUFBTSxHQUFHLENBQUMsR0FBRyxDQUFDLENBQUMsQ0FBUSw0QkFBNEI7SUFDdkQsT0FBTyxNQUFNLENBQUM7QUFDbEIsQ0FBQztBQUVELFNBQVMsSUFBSTtJQUNULElBQUksR0FBRyxHQUFHLEdBQUcsQ0FBQyxDQUFDLEVBQUUsQ0FBQyxDQUFDLENBQUMsQ0FBTyw2QkFBNkI7SUFDeEQsSUFBSSxPQUFPLEdBQUcsQ0FBQyxHQUFHLENBQUMsQ0FBQyxDQUFPLFVBQVU7SUFFckMsbURBQW1EO0lBQ25ELElBQUksR0FBRyxHQUFHLENBQUMsRUFBRSxDQUFDLENBQWEsaUNBQWlDO1FBQ3hELElBQUksR0FBRyxHQUFHLE1BQU0sR0FBRyxHQUFHLENBQUM7SUFDM0IsQ0FBQztJQUVELHFCQUFxQjtJQUNyQixTQUFTLENBQWtCLGdDQUFnQztJQUUzRCxJQUFJLFNBQVMsR0FBRyxHQUFHLEdBQUcsT0FBTyxDQUFDO0lBQzlCLE9BQU8sQ0FBQyxHQUFHLENBQUMsUUFBUSxHQUFHLFNBQVMsQ0FBQyxDQUFDO0FBQ3RDLENBQUM7QUFFRCxJQUFJLEVBQUUsQ0FBQyJ9