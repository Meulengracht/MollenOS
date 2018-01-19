/* This is the prototype source code file for Macia
 * Test various stuff, Macia is purely object oriented.
 */
//#use System;

/* Test this is Macia comment */
const string MyVar = "Hey";

object Program {

	/* Variables */
	string privstr = "This Is Private";

	/* An example method that will run in it's own 
         * thread */
	//[Async, NoReturn]
	func ProcessWork() {
	
	}

	/* Main method */
	func Main() {
		int test = 0;
		int newint = 122352;
		/* Test variables in function */
		string test2 = "local test string";

		test = 1252352 * (25 + 57) / 2 + newint * 2;
	}

	func Sec() { }
}