/* This is the prototype source code file for Macia
 * Test various stuff, Macia is purely object oriented.
 * This is a comment block
 */
import System;

namespace Test {
    // Global variables must be packed in namespaces
    readonly string MyVar = "Hey";

    object Program {

        // Example for a private string with public getter and setter
        string teststr = "This Is Public" { get: GetTestString; set: SetTestString }

        // Example for a private string
        string veryprivatestr = "This is very private";

        // Example for an asynchronous method
        [Async, NoReturn]
        func ProcessWork() {
        
        }
        
        // The entry point of any application in macia is Program.Main
        func Main() {
            int test = 0;
            int newint = 122352;
            string test2 = "local test string";

            test = 1252352 * (25 + 57) / 2 + newint * 2;
        }

        func Sec() { }
    }
}
