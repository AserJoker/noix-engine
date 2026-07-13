#include <gtest/gtest.h>
#include "script/JSEngine.h"

using namespace noix::script;

TEST(JSEngineTest, EvalArithmetic) {
    JSEngine engine;
    EXPECT_EQ(engine.eval("1 + 2"), "3");
}

TEST(JSEngineTest, EvalStringConcat) {
    JSEngine engine;
    EXPECT_EQ(engine.eval("'hello' + ' ' + 'world'"), "hello world");
}

TEST(JSEngineTest, EvalSyntaxError) {
    JSEngine engine;
    std::string result = engine.eval("invalid syntax here!!!");
    EXPECT_FALSE(result.empty());
    // Error messages vary, just verify it doesn't crash and returns something
}

TEST(JSEngineTest, EvalUndefined) {
    JSEngine engine;
    std::string result = engine.eval("typeof nonexistentVar");
    EXPECT_EQ(result, "undefined");
}

TEST(JSEngineTest, EvalComplexExpression) {
    JSEngine engine;
    EXPECT_EQ(engine.eval("(function() { return 40 + 2; })()"), "42");
}

TEST(JSEngineTest, EvalMultipleTimes) {
    JSEngine engine;
    EXPECT_EQ(engine.eval("10 * 5"), "50");
    EXPECT_EQ(engine.eval("100 / 4"), "25");
    EXPECT_EQ(engine.eval("7 - 3"), "4");
}
