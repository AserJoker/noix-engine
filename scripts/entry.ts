import { info } from "noix:logger";
import * as config from "noix:config";
import { i18n } from "noix:locale";
info(i18n("noix:system.window.title"));
info(
  `application config : ${JSON.stringify(config.get("noix:application", {} as Record<string, unknown>))}`,
);
