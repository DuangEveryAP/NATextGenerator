'use strict';
/**
 * 用最小 DOM 桩在 Node 里直接跑 web/app.js，验证前端逻辑（无需浏览器）：
 *   1. 文字变化后，贝塞尔曲面边框会按新的文字包围盒重新生成（cp 跟着 bbox 走）；
 *   2. 正在输入的坐标框不会被定时刷新覆盖（小数点不会被冲掉）；
 *   3. 归一化/实测单位切换时控制点按比例换算、曲面不变。
 *
 * 用法: node tests/web_logic.js
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.dirname(__dirname);
const html = fs.readFileSync(path.join(ROOT, 'web', 'index.html'), 'utf8');
const js = fs.readFileSync(path.join(ROOT, 'web', 'app.js'), 'utf8');

let failures = 0;
function check(name, cond, extra) {
    console.log((cond ? '  ok   ' : '  FAIL ') + name + (extra ? '   ' + extra : ''));
    if (!cond) failures++;
}

// ---------------- 最小 DOM ----------------

const fillTexts = [];      // 记录 canvas.fillText 调用，用来验证"逐字绘制参考字"
const ctxStub = new Proxy({}, {
    get(t, k) {
        if (k === 'fillText') {
            return (text, x, y) => { fillTexts.push({ text: String(text), x: x, y: y }); };
        }
        if (k === 'getImageData') {
            return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h });
        }
        if (k === 'canvas') return { width: 800, height: 600 };
        if (k === 'measureText') return () => ({ width: 10 });
        return () => undefined;
    },
    set() { return true; },
});

function makeEl(tag) {
    const el = {
        tagName: String(tag || 'div').toUpperCase(),
        children: [],
        options: [],
        _attrs: {},
        _listeners: {},
        className: '',
        textContent: '',
        value: '',
        hidden: false,
        title: '',
        type: '',
        checked: false,
        width: 800,
        height: 600,
        dataset: {},
        style: {},
        parentElement: null,
        classList: {
            _s: new Set(),
            add(c) { this._s.add(c); },
            remove(c) { this._s.delete(c); },
            contains(c) { return this._s.has(c); },
            toggle(c, v) {
                if (v === undefined) {
                    this._s.has(c) ? this._s.delete(c) : this._s.add(c);
                } else if (v) this._s.add(c);
                else this._s.delete(c);
            },
        },
        appendChild(c) {
            this.children.push(c);
            if (this.tagName === 'SELECT') this.options.push(c);
            c.parentElement = this;
            return c;
        },
        setAttribute(k, v) { this._attrs[k] = String(v); },
        removeAttribute(k) { delete this._attrs[k]; },
        getAttribute(k) { return this._attrs[k]; },
        addEventListener(t, f) { (this._listeners[t] = this._listeners[t] || []).push(f); },
        removeEventListener() {},
        click() { this.dispatch('click'); },
        dispatch(type, ev) {
            const list = this._listeners[type] || [];
            for (const f of list) f(Object.assign({ type: type, preventDefault() {}, clientX: 0, clientY: 0 }, ev || {}));
        },
        getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 600, right: 800, bottom: 600 }; },
        getContext() { return ctxStub; },
        focus() { doc.activeElement = this; },
        blur() { if (doc.activeElement === this) doc.activeElement = null; this.dispatch('blur'); },
    };
    // innerHTML = '' 要清空子节点(renderCpGrid 靠它重建)
    Object.defineProperty(el, 'innerHTML', {
        get() { return el._html || ''; },
        set(v) {
            el._html = String(v);
            if (!v) el.children.length = 0;
        },
    });
    return el;
}

const byId = {};
// 从真实 index.html 里按标签+id 建元素（select 需要 tagName 正确，否则 options 不生效）
for (const m of html.matchAll(/<([a-zA-Z]+)[^>]*\bid="([^"]+)"/g)) byId[m[2]] = makeEl(m[1]);
// 画布需要一个带尺寸的父容器(.canvas-wrap)
for (const id of ['c2d', 'c3d']) {
    const wrap = makeEl('div');
    wrap.clientWidth = 800;
    wrap.clientHeight = 600;
    byId[id].parentElement = wrap;
}

// 几组动态元素
const tabButtons = ['tab-2d', 'tab-3d', 'tab-info'].map((t) => {
    const b = makeEl('button');
    b.dataset.tab = t;
    return b;
});
const tabPanes = ['tab-2d', 'tab-3d', 'tab-info'].map((t) => {
    const d = makeEl('div');
    d.id = t;
    return d;
});
const sunitButtons = ['em', 'm'].map((u) => {
    const b = makeEl('button');
    b.dataset.unit = u;
    return b;
});
const presetButtons = ['flat', 'hbend', 'vbend', 'wave', 'dome'].map((p) => {
    const b = makeEl('button');
    b.dataset.preset = p;
    return b;
});

const doc = {
    getElementById: (id) => byId[id] || null,
    createElement: (tag) => makeEl(tag),
    querySelectorAll: (sel) => {
        if (sel === '.tabs button') return tabButtons;
        if (sel === '.tab') return tabPanes;
        if (sel === '#sunit button') return sunitButtons;
        if (sel === '.preset-row button') return presetButtons;
        return [];
    },
    querySelector: (sel) => {
        if (sel === '.tabs button[data-tab="tab-3d"]') return tabButtons[1];
        if (sel === '.tabs button[data-tab="tab-2d"]') return tabButtons[0];
        return null;
    },
    activeElement: null,
    fonts: { add() {} },
};

// 表单初值（模拟 index.html 里的 value/checked）
function setVal(id, v) { if (byId[id]) byId[id].value = v; }
setVal('text', '大蓝猫叔');
setVal('size', '10');
setVal('quality', '5');
setVal('thickness', '0.2');
setVal('out', 'output.na');
setVal('snapStep', '0.05');
for (const id of ['kerning', 'quads', 'autowrite', 'showTri', 'showRef', 'snapOn']) {
    if (byId[id]) byId[id].checked = true;
}
if (byId['font']) {
    byId['font'].options = [];
    byId['font'].append = function (o) { this.options.push(o); };
}

// ---------------- 假定时器 / fetch ----------------

let timerSeq = 1;
const timers = new Map();
function fakeSetTimeout(fn, ms) {
    const id = timerSeq++;
    timers.set(id, fn);
    return id;
}
function fakeClearTimeout(id) { timers.delete(id); }
async function runTimers(rounds) {
    for (let i = 0; i < (rounds || 40); i++) {
        await new Promise((r) => setImmediate(r));       // 让 fetch 的 promise 结算
        if (!timers.size) return;
        const list = Array.from(timers.entries());
        timers.clear();
        for (const [, fn] of list) fn();
    }
}

const requests = [];
function boundsFor(text) {
    const len = Math.max(Array.from(text).length, 1);
    return { minX: 0.04, maxX: 0.04 + 0.96 * len, minY: -0.08, maxY: 0.84 };
}

function makeResponse(params) {
    const b = boundsFor(params.get('text') || '');
    const isSurface = params.get('placement') === 'surface';
    return {
        ok: true,
        font: params.get('font'),
        family: 'Test',
        style: 'Regular',
        text: params.get('text'),
        size: 10,
        tolerance: 0.002,
        thickness: 0.2,
        placement: isSurface ? 'surface' : 'plane',
        bounds: b,
        surface: isSurface ? { cp: new Array(48).fill(0), grid: new Array(300).fill(0), gridSize: 10, flipNormal: false } : undefined,
        stats: {
            codepoints: 1, glyphs: 1, blanks: 0, rings: 1, regions: 1, holes: 0, holesDropped: 0,
            degenerate: 0, trianglesIn: 10, parts: 5, quads: 4, flips: 1, sliversIn: 0, sliversOut: 0,
            bigThinIn: 0, bigThinOut: 0, bigThinShareIn: 0, bigThinShareOut: 0,
            minAngleIn: 10, minAngleOut: 20, geometryMs: 1,
        },
        polygons: [3, 0, 0, 1, 0, 1, 1],
        solid: [3, 0, 0, 0, 1, 0, 0, 1, 1, 0, -0.1, 0, 0, -0.1, 1, 0, -0.1, 1, 1],
        // 每个字符的落笔位置(em) + 码点，模拟服务端返回
        layout: (function () {
            const text = params.get('text') || '';
            const out = [];
            let x = 0;
            for (const ch of text) {
                out.push(Number(x.toFixed(3)), ch.codePointAt(0));
                x += 0.5;
            }
            return out;
        })(),
        truncated: false,
        solidTruncated: false,
        written: 'output.na',
        parts: 5,
        requestMs: 5,
        warnings: [],
    };
}

function makeFetch() {
    return async function (url, opts) {
        if (url === '/api/fonts') {
            return { async json() { return { ok: true, fonts: [{ name: 'F.otf' }], default: 'F.otf' }; } };
        }
        if (url === '/api/ping') {
            return { async json() { return { ok: true, version: '0.4.0' }; } };
        }
        if (url.indexOf('/api/font?') === 0) throw new Error('no font in test');
        if (url === '/api/generate') {
            const params = new URLSearchParams(opts.body);
            requests.push(Object.fromEntries(params.entries()));
            const resp = makeResponse(params);
            return { async json() { return resp; } };
        }
        throw new Error('unexpected url ' + url);
    };
}

// ---------------- 运行 app.js ----------------

const sandbox = {
    document: doc,
    window: { devicePixelRatio: 1, addEventListener() {} },
    setTimeout: fakeSetTimeout,
    clearTimeout: fakeClearTimeout,
    setInterval: () => 0,
    clearInterval: () => {},
    fetch: makeFetch(),
    URLSearchParams,
    console,
    FontFace: class { constructor() {} async load() { } },
    Math, Number, Array, Object, JSON, isFinite, parseFloat, parseInt, String, Boolean,
    Uint8ClampedArray, Promise, Error, Proxy,
};
sandbox.window.document = doc;
vm.createContext(sandbox);
vm.runInContext(js, sandbox, { filename: 'app.js' });

(async () => {
    await runTimers(60);   // boot -> 首次生成(可能还会因为边框重算再生成一次)

    console.log('\n[1] 文字变化后重新确定曲面边框');
    // 切到曲面模式
    byId['placement'].value = 'surface';
    byId['placement'].dispatch('change');
    await runTimers(60);
    const surfReqs = requests.filter((r) => r.placement === 'surface');
    check('曲面模式已发出请求', surfReqs.length > 0, '共 ' + surfReqs.length + ' 次');

    // 记下当前 cp（应与当前文字包围盒匹配）
    const cpOf = (r) => r.cp.trim().split(/\s+/).map((s) => s.split(',').map(Number));
    let cpLast = cpOf(surfReqs[surfReqs.length - 1]);
    const zMax = cpLast.reduce((m, p) => Math.max(m, p[2]), -1e9);
    let bbox = boundsFor(byId['text'].value);
    check('cp 的 z 最大值 ≈ 包围盒 maxX(em)',
        Math.abs(zMax - bbox.maxX) < 0.02, 'z=' + zMax.toFixed(2) + ' 期望≈' + bbox.maxX.toFixed(2));

    // 改文字：包围盒应变宽，cp 应随之重建
    byId['text'].value = '大蓝猫叔大岛';
    byId['text'].dispatch('input');
    await runTimers(80);
    const surfReqs2 = requests.filter((r) => r.placement === 'surface');
    const cpNew = cpOf(surfReqs2[surfReqs2.length - 1]);
    const zMaxNew = cpNew.reduce((m, p) => Math.max(m, p[2]), -1e9);
    const bboxNew = boundsFor('大蓝猫叔大岛');
    check('文字变长后 cp 的 z 范围跟着变大',
        zMaxNew > zMax + 1.0, 'z 从 ' + zMax.toFixed(2) + ' 变成 ' + zMaxNew.toFixed(2));
    check('新的 z 最大值 ≈ 新包围盒 maxX(em)',
        Math.abs(zMaxNew - bboxNew.maxX) < 0.02, 'z=' + zMaxNew.toFixed(2) + ' 期望≈' + bboxNew.maxX.toFixed(2));

    console.log('\n[2] 正在输入的坐标框不被定时刷新覆盖');
    // 选中一个控制点
    byId['cpGrid'].children.length;   // 网格由脚本构建
    const gridBtns = byId['cpGrid'].children.filter((c) => c.className && c.className.indexOf('cpbtn') >= 0);
    check('控制点网格已生成 16 个按钮', gridBtns.length === 16, '实际 ' + gridBtns.length);
    gridBtns[5].dispatch('click');
    await runTimers(40);
    check('选中后 X 输入框有值', byId['cpx'].value !== '', 'cpx=' + byId['cpx'].value);

    // 模拟"正在输入 1."：聚焦并输入，随后触发一次生成（会走 syncCpInputs）
    byId['cpx'].focus();
    byId['cpx'].value = '1.';
    byId['cpx'].dispatch('input');
    await runTimers(60);
    check('输入过程中小数点没有被冲掉', byId['cpx'].value === '1.', '实际 "' + byId['cpx'].value + '"');

    // 失去焦点后应被规整
    byId['cpx'].blur();
    await runTimers(40);
    check('失焦后格式规整为 1', byId['cpx'].value === '1', '实际 "' + byId['cpx'].value + '"');

    console.log('\n[3] 单位切换：数值换算、曲面尺寸不变');
    const before = requests.filter((r) => r.placement === 'surface').pop();
    const cpBefore = cpOf(before);
    const sizeVal = parseFloat(byId['size'].value);
    sunitButtons[1].dispatch('click');    // 切到 实测(米)
    await runTimers(60);
    const after = requests.filter((r) => r.placement === 'surface').pop();
    const cpAfter = cpOf(after);
    check('切换后单位标记为 m', after.surfaceUnit === 'm', 'surfaceUnit=' + after.surfaceUnit);
    const ratio = cpAfter[15][2] !== 0 ? cpAfter[15][2] / cpBefore[15][2] : 0;
    check('控制点按字号换算(×' + sizeVal + ')', Math.abs(ratio - sizeVal) < 0.05, 'ratio=' + ratio.toFixed(3));

    console.log('\n[4] 预设：水平弯应让 x 出现鼓包');
    presetButtons[1].dispatch('click');   // hbend
    await runTimers(60);
    const hb = cpOf(requests.filter((r) => r.placement === 'surface').pop());
    const xMax = hb.reduce((m, p) => Math.max(m, Math.abs(p[0])), 0);
    check('水平弯后控制点 x 明显非零', xMax > 1.0, 'max|x|=' + xMax.toFixed(2));

    console.log('\n[5] 手动改点后换文字 -> 重置为平直；仅用预设 -> 保持形状');
    // 当前是 hbend 预设(米单位)：换文字后弯曲应保留、边框跟着变
    presetButtons[1].dispatch('click');
    await runTimers(60);
    const cpPresetBefore = cpOf(requests.filter((r) => r.placement === 'surface').pop());
    const hbX = cpPresetBefore.reduce((m, p) => Math.max(m, Math.abs(p[0])), 0);
    byId['text'].value = '大蓝猫叔大岛大厨';
    byId['text'].dispatch('input');
    await runTimers(80);
    const cpPresetAfter = cpOf(requests.filter((r) => r.placement === 'surface').pop());
    const hbX2 = cpPresetAfter.reduce((m, p) => Math.max(m, Math.abs(p[0])), 0);
    const zAfter = cpPresetAfter.reduce((m, p) => Math.max(m, p[2]), -1e9);
    const bbox5 = boundsFor('大蓝猫叔大岛大厨');
    check('预设弯曲在换文字后保留', hbX2 > 1.0, 'max|x| ' + hbX.toFixed(2) + ' -> ' + hbX2.toFixed(2));
    check('边框按新文字变宽', Math.abs(zAfter - bbox5.maxX * 10) < 0.2, 'z=' + zAfter.toFixed(2));

    // 手动改一个点，再换文字 -> 重置为平直(全部 x≈0)
    const btns2 = byId['cpGrid'].children.filter((c) => c.className && c.className.indexOf('cpbtn') >= 0);
    btns2[6].dispatch('click');
    await runTimers(40);
    byId['cpx'].value = '2.5';
    byId['cpx'].dispatch('input');
    await runTimers(80);
    byId['text'].value = '大蓝猫叔大岛大厨雪';
    byId['text'].dispatch('input');
    await runTimers(80);
    const cpReset = cpOf(requests.filter((r) => r.placement === 'surface').pop());
    const maxXReset = cpReset.reduce((m, p) => Math.max(m, Math.abs(p[0])), 0);
    check('手动改点后换文字 -> 重置为平直', maxXReset < 1e-6, 'max|x|=' + maxXReset.toFixed(6));

    console.log('\n[6] 参考字按 layout 逐字绘制（避免浏览器 GPOS 字距导致右侧错位）');
    byId['placement'].value = 'plane';
    byId['placement'].dispatch('change');
    byId['text'].value = 'Navalart';
    byId['text'].dispatch('input');
    await runTimers(80);
    fillTexts.length = 0;
    byId['showRef'].dispatch('change');       // 触发一次 2D 重绘
    if (process.env.NATG_DEBUG) {
        console.log('    [debug] 所有 fillText: ' + JSON.stringify(fillTexts.map((f) => [f.text, Math.round(f.x)])));
    }
    // 3D 视图也会调用 fillText(提示文字/轴标签)，这里只取"逐字"调用(单字符)
    const perGlyph = fillTexts.filter((f) => Array.from(f.text).length === 1);
    check('逐字调用 fillText（8 个字符）', perGlyph.length === 8, '实际 ' + perGlyph.length + ' 次');
    const texts = perGlyph.map((f) => f.text).join('');
    check('逐字绘制的字符顺序正确', texts === 'Navalart', '"' + texts + '"');
    const xsSorted = perGlyph.every((f, i) => i === 0 || f.x > perGlyph[i - 1].x);
    check('逐字 x 坐标严格递增', xsSorted,
        perGlyph.map((f) => Math.round(f.x)).join(','));
    const gaps = perGlyph.slice(1).map((f, i) => f.x - perGlyph[i].x);
    const uniform = gaps.every((g) => Math.abs(g - gaps[0]) < 0.5);
    check('字距按 layout 等距（未被浏览器重排）', gaps.length > 0 && uniform,
        'Δx=' + gaps.map((g) => Math.round(g)).join(','));

    console.log('\n' + (failures ? 'RESULT: ' + failures + ' 项失败' : 'RESULT: OK'));
    process.exit(failures ? 1 : 0);
})();
