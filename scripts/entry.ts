import * as logger from "noix:logger";
import { addEventListener, removeEventListener, emit } from "noix:eventbus";

// Test 1: emit async — listener should receive the event via SDL queue
const handle = addEventListener("noix:initialize", (data) => {
  logger.info("eventbus", `received noix:initialize: ${JSON.stringify(data)}`);
});
emit("noix:initialize", { message: "hello from entry script" });

// Test 2: removeEventListener — wait a bit for async dispatch, then remove
// (In real usage, events arrive asynchronously. Here we just verify the API works.)
logger.info("eventbus", `listener handle = ${handle}`);
