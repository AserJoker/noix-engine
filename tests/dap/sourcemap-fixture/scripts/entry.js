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
//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoiZW50cnkuanMiLCJzb3VyY2VSb290IjoiIiwic291cmNlcyI6WyJlbnRyeS50cyJdLCJuYW1lcyI6W10sIm1hcHBpbmdzIjoiO0FBQUEsNEVBQTRFO0FBQzVFLHdFQUF3RTtBQUV4RSxJQUFJLENBQUMsR0FBRyxFQUFFLENBQUM7QUFDWCxJQUFJLENBQUMsR0FBRyxFQUFFLENBQUM7QUFFWCxTQUFTLEdBQUcsQ0FBQyxDQUFTLEVBQUUsQ0FBUztJQUM3QixJQUFJLE1BQU0sR0FBRyxDQUFDLEdBQUcsQ0FBQyxDQUFDLENBQVEsNEJBQTRCO0lBQ3ZELE9BQU8sTUFBTSxDQUFDO0FBQ2xCLENBQUM7QUFFRCxTQUFTLElBQUk7SUFDVCxJQUFJLEdBQUcsR0FBRyxHQUFHLENBQUMsQ0FBQyxFQUFFLENBQUMsQ0FBQyxDQUFDLENBQU8sNkJBQTZCO0lBQ3hELElBQUksT0FBTyxHQUFHLENBQUMsR0FBRyxDQUFDLENBQUMsQ0FBTyxVQUFVO0lBRXJDLG1EQUFtRDtJQUNuRCxJQUFJLEdBQUcsR0FBRyxDQUFDLEVBQUUsQ0FBQyxDQUFhLGlDQUFpQztRQUN4RCxJQUFJLEdBQUcsR0FBRyxNQUFNLEdBQUcsR0FBRyxDQUFDO0lBQzNCLENBQUM7SUFFRCxxQkFBcUI7SUFDckIsU0FBUyxDQUFrQixnQ0FBZ0M7SUFFM0QsSUFBSSxTQUFTLEdBQUcsR0FBRyxHQUFHLE9BQU8sQ0FBQztJQUM5QixPQUFPLENBQUMsR0FBRyxDQUFDLFFBQVEsR0FBRyxTQUFTLENBQUMsQ0FBQztBQUN0QyxDQUFDO0FBRUQsSUFBSSxFQUFFLENBQUMifQ==