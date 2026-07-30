import { info } from "noix:logger";
import * as config from "noix:config";
info(
  `application config : ${JSON.stringify(config.get("noix:application", {} as Record<string, unknown>))}`,
);
