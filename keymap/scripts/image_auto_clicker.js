// 找图连点器
// 用法（直接粘到键位脚本中）：
// var ac = mapi.loadModule('image_auto_clicker.js');
// ac.run({
//     template: 'target.png',
//     regionId: 1,
//     threshold: 0.85,
//     clickIntervalMs: 40,
//     searchIntervalMs: 80
// });
//
// 也可不用 regionId，改成整屏或局部搜索：
// ac.run({
//     template: 'target.png',
//     region: { x1: 0.2, y1: 0.3, x2: 0.8, y2: 0.9 },
//     threshold: 0.82,
//     clickOnFoundPosition: true
// });

function clamp01(value, fallbackValue) {
    if (typeof value !== 'number' || !isFinite(value)) {
        return fallbackValue;
    }
    if (value < 0) return 0;
    if (value > 1) return 1;
    return value;
}

function toPositiveInt(value, fallbackValue) {
    if (typeof value !== 'number' || !isFinite(value)) {
        return fallbackValue;
    }
    value = Math.floor(value);
    return value > 0 ? value : fallbackValue;
}

function normalizeConfig(options) {
    options = options || {};

    var region = options.region || {};
    var x1 = clamp01(region.x1, 0);
    var y1 = clamp01(region.y1, 0);
    var x2 = clamp01(region.x2, 1);
    var y2 = clamp01(region.y2, 1);

    if (x2 < x1) {
        var tx = x1;
        x1 = x2;
        x2 = tx;
    }
    if (y2 < y1) {
        var ty = y1;
        y1 = y2;
        y2 = ty;
    }

    return {
        template: options.template || 'target.png',
        regionId: toPositiveInt(options.regionId, 0),
        x1: x1,
        y1: y1,
        x2: x2,
        y2: y2,
        threshold: clamp01(options.threshold, 0.80),
        clickIntervalMs: toPositiveInt(options.clickIntervalMs, 50),
        searchIntervalMs: toPositiveInt(options.searchIntervalMs, 120),
        clickOnFoundPosition: options.clickOnFoundPosition === true,
        toastOnStart: options.toastOnStart !== false,
        toastOnStop: options.toastOnStop !== false,
        debugLog: options.debugLog === true
    };
}

function findTarget(config) {
    if (config.regionId > 0) {
        return mapi.findImageByRegion(config.template, config.regionId, config.threshold);
    }

    return mapi.findImage(
        config.template,
        config.x1,
        config.y1,
        config.x2,
        config.y2,
        config.threshold
    );
}

function safeRelease() {
    mapi.releaseAll();
}

export function run(options) {
    var config = normalizeConfig(options);
    var lockedPos = null;

    if (!mapi.isPress()) {
        safeRelease();
        return;
    }

    if (config.toastOnStart) {
        mapi.toast('找图连点已启动', 1200);
    }

    try {
        while (!mapi.isInterrupted()) {
            var result = findTarget(config);

            if (result && result.found) {
                lockedPos = { x: result.x, y: result.y };

                if (config.debugLog) {
                    mapi.log('[image_auto_clicker] found=' + config.template
                        + ' x=' + result.x + ' y=' + result.y
                        + ' confidence=' + result.confidence);
                }

                break;
            }

            mapi.sleep(config.searchIntervalMs);
        }

        while (!mapi.isInterrupted() && lockedPos) {
            if (config.clickOnFoundPosition) {
                mapi.click(lockedPos.x, lockedPos.y);
            } else {
                mapi.click();
            }

            mapi.sleep(config.clickIntervalMs);
        }
    } finally {
        safeRelease();
        if (config.toastOnStop) {
            mapi.toast('找图连点已停止', 1000);
        }
    }
}
