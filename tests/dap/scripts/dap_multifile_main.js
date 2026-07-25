// Main script that imports a module
import { add, greeting } from "./dap_multifile_mod.js";

var x = 10;               // line 3
var y = 20;               // line 4
var result = add(x, y);   // line 5
console.log(greeting);    // line 6
console.log(result);      // line 7
