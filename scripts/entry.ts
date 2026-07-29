import { info } from "noix:logger";
import { registerCommand } from "noix:debug";
registerCommand("script/test", () => {
  info("script command attched");
  return {};
});
