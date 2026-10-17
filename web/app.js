'use strict';

(function () {
	const $ = (id) => document.getElementById(id);

	const state = {
		res: null,
		polys: [],
		previewFamily: null,
		previewKey: null,
		generating: false,
		dirty: false,
		writtenName: null,
		view: { S: 1, ox: 0, oy: 0, W: 1, H: 1 },
		geo3d: null,
		v3: { yaw: 0.62, pitch: 0.28, zoom: 1, dragMode: false, lx: 0, ly: 0, anchor: false },
		surfaceUnit: 'em',
		cp: null,          // 16 个控制点 [x,y,z]，单位与 surfaceUnit 一致
		cpPreset: 'flat',  // 最近一次用的预设(文字变化时按它重新取边框)
		cpEdited: false,   // 控制点是否被手动改过
		cpKey: null,       // 生成这批控制点时使用的文字包围盒/尺度
		selCp: -1,         // 当前选中的控制点
		axisDrag: null,    // 正在沿某轴拖动
		cpPx: [],          // 控制点在画布上的像素位置(命中测试用)
		gizmoPx: [],       // 轴柄线段(命中测试用)
		proj: null,        // 当前相机投影函数
	};

	let fontSeq = 0;
	let debounceTimer = 0;

	const COLOR = {
		bg: '#14171c',
		tri: [232, 69, 60],
		ref: [53, 196, 106],
		both: [232, 211, 60],
	};

	const AXIS_COLOR = ['#ff6b6b', '#6fdc7f', '#5aa9ff'];   // X / Y / Z

	function setStatus(text, cls) {
		const el = $('status');
		el.textContent = text;
		el.className = 'status' + (cls ? ' ' + cls : '');
	}

	function fmtNum(v) {
		let s = Number(v).toFixed(3);
		if (s.indexOf('.') >= 0) s = s.replace(/0+$/, '').replace(/\.$/, '');
		if (s === '-0') s = '0';
		return s;
	}

	function clamp(v, a, b) {
		return v < a ? a : (v > b ? b : v);
	}

	// ---------------- 控制点状态 ----------------

	function unitScale() {
		// 编辑单位 -> 世界(米): 归一化(em) 乘字号，实测(米) 不缩放
		return state.surfaceUnit === 'em' ? (parseFloat($('size').value) || 10) : 1;
	}

	function ensureCp() {
		if (!state.cp) {
			state.cp = makePresetArray(state.cpPreset || 'flat', null);
			state.cpKey = null;      // 还没和真实文字包围盒对齐，第一次生成后会重新取边框
		}
		return state.cp;
	}

	function cpWorld(i) {
		const s = unitScale();
		const p = state.cp[i];
		return [p[0] * s, p[1] * s, p[2] * s];
	}

	function cpWorldArray() {
		const out = [];
		for (let i = 0; i < 16; i++) out.push(cpWorld(i));
		return out;
	}

	function cpToString() {
		if (!state.cp) return '';
		return state.cp.map((p) => p.map(fmtNum).join(',')).join(' ');
	}

	function makePresetArray(kind, boundsIn) {
		const res = state.res;
		const size = parseFloat($('size').value) || 10;
		const k = state.surfaceUnit === 'em' ? 1 : size;    // em 倍数 / 米
		const b = boundsIn || (res && res.bounds) || { minX: 0, maxX: 6, minY: 0, maxY: 1 };
		const W = Math.max(b.maxX - b.minX, 1e-6) * k;
		const H = Math.max(b.maxY - b.minY, 1e-6) * k;
		const peak = 0.25 * W;
		const arc = peak / 0.75;                    // 三次贝塞尔控制值 -> 峰值
		const out = [];
		for (let j = 0; j < 4; j++) {
			const v = j / 3;
			const y = (b.minY + (b.maxY - b.minY) * v) * k;
			for (let i = 0; i < 4; i++) {
				const u = i / 3;
				const z = b.minX * k + W * u;
				let x = 0;
				const arcU = (i === 1 || i === 2) ? 1 : 0;
				const arcV = (j === 1 || j === 2) ? 1 : 0;
				if (kind === 'hbend') x = arcU * arc;
				else if (kind === 'vbend') x = arcV * arc;
				else if (kind === 'wave') x = (i === 1 ? arc : (i === 2 ? -arc : 0));
				else if (kind === 'dome') x = peak / 0.5625 * (arcU * arcV);
				out.push([x, y, z]);
			}
		}
		return out;
	}

	// 文字/字号变化后，贝塞尔曲面的边框需要重新确定(重设为平直并按新包围盒调整大小)
	function refitSurfaceToText(bounds) {
		if ($('placement').value !== 'surface') return false;
		const size = parseFloat($('size').value) || 10;
		const scale = state.surfaceUnit === 'em' ? 1 : size;   // 实测模式下字号也会影响边框
		const next = {
			minX: bounds.minX, maxX: bounds.maxX, minY: bounds.minY, maxY: bounds.maxY, scale: scale,
		};
		const cur = state.cpKey;
		const changed = !cur || !state.cp ||
			Math.abs(next.minX - cur.minX) > 1e-6 || Math.abs(next.maxX - cur.maxX) > 1e-6 ||
			Math.abs(next.minY - cur.minY) > 1e-6 || Math.abs(next.maxY - cur.maxY) > 1e-6 ||
			Math.abs(next.scale - cur.scale) > 1e-9;
		if (!changed) return false;
		// 手动改过点的话按"重置为平面"处理，否则沿用当前预设的形状
		const kind = state.cpEdited ? 'flat' : (state.cpPreset || 'flat');
		state.cp = makePresetArray(kind, bounds);
		state.cpPreset = kind;
		state.cpEdited = false;
		state.cpKey = next;
		state.selCp = -1;
		renderCpGrid();
		syncCpInputs();
		return true;
	}

	function setUnit(unit) {
		if (state.surfaceUnit === unit) return;
		const size = parseFloat($('size').value) || 10;
		const f = (unit === 'em') ? (1 / size) : size;   // em -> m 乘 size；m -> em 除 size
		if (state.cp) {
			for (const p of state.cp) {
				p[0] *= f;
				p[1] *= f;
				p[2] *= f;
			}
		}
		const stepEl = $('snapStep');
		const step = parseFloat(stepEl.value);
		if (isFinite(step)) stepEl.value = fmtNum(step * f);
		state.surfaceUnit = unit;
		if (state.cpKey) {
			// 控制点数值已换算，边框的实际尺寸没变，只更新尺度记录
			state.cpKey.scale = (unit === 'em') ? 1 : size;
		}
		document.querySelectorAll('#sunit button').forEach((b) => {
			b.classList.toggle('active', b.dataset.unit === unit);
		});
		syncCpInputs();
		renderCpGrid();
		draw3D();
		scheduleGenerate(0);
	}

	// ---------------- 控制点 UI ----------------

	function renderCpGrid() {
		const grid = $('cpGrid');
		grid.innerHTML = '';
		const cell = (text, cls) => {
			const d = document.createElement('div');
			d.className = cls || 'cplabel';
			d.textContent = text;
			return d;
		};
		grid.appendChild(cell(''));
		for (let i = 0; i < 4; i++) grid.appendChild(cell('u' + i));
		for (let r = 0; r < 4; r++) {
			const j = 3 - r;
			grid.appendChild(cell('v' + j));
			for (let i = 0; i < 4; i++) {
				const idx = j * 4 + i;
				const btn = document.createElement('button');
				btn.type = 'button';
				btn.className = 'cpbtn' + (state.selCp === idx ? ' sel' : '');
				btn.dataset.idx = String(idx);
				btn.title = 'u' + i + ' / v' + j;
				const dot = document.createElement('i');
				btn.appendChild(dot);
				btn.addEventListener('click', () => selectCp(idx));
				grid.appendChild(btn);
			}
		}
	}

	function selectCp(idx) {
		state.selCp = idx;
		renderCpGrid();
		syncCpInputs(true);
		draw3D();
	}

	function setNumInput(id, v, force) {
		const el = $(id);
		if (!el) return;
		// 正在这个框里输入时绝对不要回写，否则 "1." 会被格式化成 "1"，小数点被冲掉
		if (!force && document.activeElement === el) return;
		const cur = parseFloat(el.value);
		if (!force && isFinite(cur) && Math.abs(cur - v) < 1e-9) return;   // 数值没变就不重写
		el.value = fmtNum(v);
	}

	function syncCpInputs(force) {
		const label = $('cpSelLabel');
		const idx = state.selCp;
		if (idx < 0 || !state.cp) {
			label.textContent = '未选中控制点（点上方圆点或 3D 里的圆点）';
			label.className = '';
			['cpx', 'cpy', 'cpz'].forEach((id) => {
				if (document.activeElement !== $(id)) $(id).value = '';
			});
			return;
		}
		const i = idx % 4;
		const j = Math.floor(idx / 4);
		label.textContent = '编辑控制点 u' + i + ' / v' + j;
		label.className = 'on';
		setNumInput('cpx', state.cp[idx][0], force);
		setNumInput('cpy', state.cp[idx][1], force);
		setNumInput('cpz', state.cp[idx][2], force);
	}

	function applyCpInputs() {
		const idx = state.selCp;
		if (idx < 0 || !state.cp) return;
		const vals = ['cpx', 'cpy', 'cpz'].map((id) => parseFloat($(id).value));
		let touched = false;
		for (let k = 0; k < 3; k++) {
			if (isFinite(vals[k]) && Math.abs(vals[k] - state.cp[idx][k]) > 1e-12) {
				state.cp[idx][k] = vals[k];
				touched = true;
			}
		}
		if (!touched) return;
		state.cpEdited = true;
		draw3D();
		scheduleGenerate(220);
	}

	function setPreset(kind) {
		state.cpPreset = kind;
		state.cpEdited = false;
		const bounds = state.res ? state.res.bounds : { minX: 0, maxX: 6, minY: 0, maxY: 1 };
		const size = parseFloat($('size').value) || 10;
		state.cp = makePresetArray(kind, bounds);
		state.cpKey = {
			minX: bounds.minX, maxX: bounds.maxX, minY: bounds.minY, maxY: bounds.maxY,
			scale: state.surfaceUnit === 'em' ? 1 : size,
		};
		state.selCp = -1;
		renderCpGrid();
		syncCpInputs(true);
		draw3D();
		scheduleGenerate(0);
	}

	// ---------------- 贝塞尔曲面(本地求值，拖动时即时反馈) ----------------

	function bern(i, t) {
		const m = 1 - t;
		return [m * m * m, 3 * t * m * m, 3 * t * t * m, t * t * t][i];
	}

	function patchPoint(cpArr, u, v) {
		let x = 0, y = 0, z = 0;
		for (let j = 0; j < 4; j++) {
			const bv = bern(j, v);
			for (let i = 0; i < 4; i++) {
				const w = bern(i, u) * bv;
				const p = cpArr[j * 4 + i];
				x += p[0] * w;
				y += p[1] * w;
				z += p[2] * w;
			}
		}
		return [x, y, z];
	}

	function updateSurfaceVisibility() {
		const on = $('placement').value === 'surface';
		$('surfacePanel').hidden = !on;
		if (on) {
			ensureCp();
			renderCpGrid();
			syncCpInputs();
		}
		$('cpInfo').textContent = on
			? '文字包围盒: ' + (state.res ? (state.res.bounds.maxX - state.res.bounds.minX).toFixed(2) + ' × ' +
				(state.res.bounds.maxY - state.res.bounds.minY).toFixed(2) + ' em' : '—')
			: '';
	}

	// ---------------- 生成 ----------------

	function gather() {
		const typedPath = $('fontPath').value.trim();
		const surface = $('placement').value === 'surface';
		if (surface) ensureCp();
		return {
			font: typedPath || $('font').value || '',
			text: $('text').value,
			size: $('size').value,
			quality: $('quality').value,
			thickness: $('thickness').value,
			kerning: $('kerning').checked ? 1 : 0,
			mergeQuads: $('quads').checked ? 1 : 0,
			write: $('autowrite').checked ? 1 : 0,
			out: $('out').value.trim() || 'output.na',
			placement: surface ? 'surface' : 'plane',
			surfaceUnit: state.surfaceUnit,
			cp: surface ? cpToString() : '',
			flipNormal: $('flipNormal').checked ? 1 : 0,
		};
	}

	async function loadFonts() {
		try {
			const res = await fetch('/api/fonts');
			const json = await res.json();
			const sel = $('font');
			sel.innerHTML = '';
			(json.fonts || []).forEach((f) => {
				const opt = document.createElement('option');
				opt.value = f.name;
				opt.textContent = f.name;
				sel.appendChild(opt);
			});
			if (json.default) {
				let found = false;
				for (const opt of sel.options) {
					if (opt.value === json.default) found = true;
				}
				if (found) sel.value = json.default;
			}
			if (!sel.options.length) {
				const opt = document.createElement('option');
				opt.value = '';
				opt.textContent = '(fonts 目录里没有字体)';
				sel.appendChild(opt);
			}
		} catch (e) {
			setStatus('读取字体列表失败: ' + e.message, 'err');
		}
	}

	async function ensurePreviewFont(key) {
		if (state.previewKey === key) return;
		state.previewKey = key;
		state.previewFamily = null;
		if (!key) return;
		try {
			const family = 'natgPreview' + ++fontSeq;
			const url = '/api/font?name=' + encodeURIComponent(key);
			const face = new FontFace(family, 'url("' + url + '")');
			await face.load();
			document.fonts.add(face);
			state.previewFamily = family;
		} catch (e) {
			state.previewFamily = null;
			setStatus('预览字体加载失败(仍可生成 .na)', 'warn');
		}
	}

	async function generate() {
		if (state.generating) {
			state.dirty = true;
			return;
		}
		const p = gather();
		if (!p.text.replace(/\s/g, '')) {
			setStatus('请输入文本', 'warn');
			return;
		}
		state.generating = true;
		setStatus('生成中…');
		try {
			const body = new URLSearchParams();
			Object.keys(p).forEach((k) => body.append(k, p[k]));
			const r = await fetch('/api/generate', { method: 'POST', body });
			const json = await r.json();
			if (!json.ok) {
				setStatus(json.error || '生成失败', 'err');
				return;
			}
			state.res = json;
			state.polys = parsePolygons(json.polygons);
			state.writtenName = json.written;
			const dl = $('download');
			if (json.written) {
				dl.classList.remove('disabled');
				dl.setAttribute('href', '/api/download?f=' + encodeURIComponent(json.written));
				dl.textContent = '下载 ' + json.written;
			} else {
				dl.classList.add('disabled');
				dl.removeAttribute('href');
				dl.textContent = '下载 .na';
			}
			$('fontInfo').textContent = (json.family ? json.family + ' ' + json.style + '  ' : '') + json.font;
			// 文字/字号变化后重新确定曲面边框(必要时按新包围盒重设控制点)
			const refit = refitSurfaceToText(json.bounds);
			await ensurePreviewFont(p.font);
			draw2D();
			build3D();
			draw3D();
			updateInfo(json);
			const warn = (json.warnings || []).length;
			setStatus('完成 ' + json.stats.parts + ' 个零件（四边形 ' + json.stats.quads + '）· ' +
				json.requestMs + ' ms' + (warn ? ' · ' + warn + ' 条提示' : ''), warn ? 'warn' : 'ok');
			if (refit) scheduleGenerate(0);   // 边框变了，用新控制点再生成一次
		} catch (e) {
			setStatus('请求失败: ' + e.message, 'err');
		} finally {
			state.generating = false;
			if (state.dirty) {
				state.dirty = false;
				generate();
			}
		}
	}

	function scheduleGenerate(delay) {
		clearTimeout(debounceTimer);
		debounceTimer = setTimeout(generate, delay === undefined ? 220 : delay);
	}

	// 服务端返回的格式: [顶点数, x0,y0, x1,y1, ...] 重复
	function parsePolygons(flat) {
		const out = [];
		if (!flat) return out;
		let i = 0;
		while (i < flat.length) {
			const n = flat[i++];
			if (!(n >= 3)) break;
			const pts = [];
			for (let k = 0; k < n && i + 1 < flat.length; k++) {
				pts.push([flat[i], flat[i + 1]]);
				i += 2;
			}
			if (pts.length >= 3) out.push(pts);
		}
		return out;
	}

	// ---------------- 2D 叠图 ----------------

	function makeMask(W, H) {
		const c = document.createElement('canvas');
		c.width = W;
		c.height = H;
		const ctx = c.getContext('2d', { willReadFrequently: true });
		ctx.fillStyle = '#000';
		ctx.fillRect(0, 0, W, H);
		return ctx;
	}

	function draw2D() {
		const res = state.res;
		const canvas = $('c2d');
		if (!canvas) return;
		const wrap = canvas.parentElement;
		const dpr = window.devicePixelRatio || 1;
		const W = Math.max(2, Math.floor(wrap.clientWidth * dpr));
		const H = Math.max(2, Math.floor(wrap.clientHeight * dpr));
		if (canvas.width !== W) canvas.width = W;
		if (canvas.height !== H) canvas.height = H;
		const ctx = canvas.getContext('2d');
		ctx.fillStyle = COLOR.bg;
		ctx.fillRect(0, 0, W, H);
		if (!res) return;

		const b = res.bounds;
		const bw = Math.max(b.maxX - b.minX, 1e-6);
		const bh = Math.max(b.maxY - b.minY, 1e-6);
		const margin = Math.round(22 * dpr);
		const S = Math.min((W - 2 * margin) / bw, (H - 2 * margin) / bh);
		const ox = margin - b.minX * S;
		const oy = H - margin + b.minY * S;
		state.view = { S: S, ox: ox, oy: oy, W: W, H: H };

		const showTri = $('showTri').checked;
		const showRef = $('showRef').checked && !!state.previewFamily;
		const showEdges = $('showEdges').checked;

		const mA = makeMask(W, H);
		const mB = makeMask(W, H);

		if (showTri) {
			const g = mA;
			g.fillStyle = '#fff';
			for (const poly of state.polys) {
				g.beginPath();
				g.moveTo(ox + poly[0][0] * S, oy - poly[0][1] * S);
				for (let k = 1; k < poly.length; k++) {
					g.lineTo(ox + poly[k][0] * S, oy - poly[k][1] * S);
				}
				g.closePath();
				g.fill();
			}
		}

		if (showRef) {
			const g = mB;
			g.fillStyle = '#fff';
			g.textBaseline = 'alphabetic';
			g.textAlign = 'left';
			g.font = S + 'px "' + state.previewFamily + '"';
			// 关掉浏览器自己的字距，否则参考字会与我们的排版逐字错位、越往右越明显
			try { g.fontKerning = 'none'; } catch (_) { /* 老浏览器忽略 */ }
			try { g.fontFeatureSettings = '"kern" 0, "liga" 0'; } catch (_) { /* 忽略 */ }
			const lay = res.layout;
			if (lay && lay.length >= 2) {
				// 按服务端给出的落笔位置逐字绘制，位置与生成结果严格一致
				for (let i = 0; i + 1 < lay.length; i += 2) {
					const x = lay[i];
					const cp = lay[i + 1];
					if (!(cp > 32)) continue;          // 空格等空白不画(位置已由 layout 决定)
					let ch;
					try {
						ch = String.fromCodePoint(cp);
					} catch (_) {
						continue;
					}
					g.fillText(ch, ox + x * S, oy);
				}
			} else {
				g.fillText(res.text, ox, oy);
			}
		}

		const a = mA.getImageData(0, 0, W, H);
		const bb = mB.getImageData(0, 0, W, H);
		const out = ctx.getImageData(0, 0, W, H);
		const ad = a.data;
		const bd = bb.data;
		const od = out.data;
		const ti = COLOR.tri;
		const ri = COLOR.ref;
		const bi = COLOR.both;
		for (let i = 0; i < od.length; i += 4) {
			const av = ad[i] > 127;
			const bv = bd[i] > 127;
			if (av && bv) {
				od[i] = bi[0]; od[i + 1] = bi[1]; od[i + 2] = bi[2];
			} else if (av) {
				od[i] = ti[0]; od[i + 1] = ti[1]; od[i + 2] = ti[2];
			} else if (bv) {
				od[i] = ri[0]; od[i + 1] = ri[1]; od[i + 2] = ri[2];
			} else {
				od[i] = 20; od[i + 1] = 23; od[i + 2] = 28;
			}
			od[i + 3] = 255;
		}
		ctx.putImageData(out, 0, 0);

		if (showEdges && showTri) {
			ctx.strokeStyle = 'rgba(255,255,255,0.22)';
			ctx.lineWidth = Math.max(1, dpr * 0.6);
			for (const poly of state.polys) {
				ctx.beginPath();
				ctx.moveTo(ox + poly[0][0] * S, oy - poly[0][1] * S);
				for (let k = 1; k < poly.length; k++) {
					ctx.lineTo(ox + poly[k][0] * S, oy - poly[k][1] * S);
				}
				ctx.closePath();
				ctx.stroke();
			}
		}
	}

	// ---------------- 3D 预览 ----------------

	function build3D() {
		const res = state.res;
		const solid = res && res.solid ? res.solid : null;
		if (!solid || !solid.length) {
			state.geo3d = null;
			return;
		}
		const faces = [];
		let i = 0;
		let count = 0;
		let minX = 1e300, minY = 1e300, minZ = 1e300;
		let maxX = -1e300, maxY = -1e300, maxZ = -1e300;
		const track = (p) => {
			if (p[0] < minX) minX = p[0];
			if (p[0] > maxX) maxX = p[0];
			if (p[1] < minY) minY = p[1];
			if (p[1] > maxY) maxY = p[1];
			if (p[2] < minZ) minZ = p[2];
			if (p[2] > maxZ) maxZ = p[2];
		};
		// 文件里是行优先 A,B,C,D；面序要按 A,B,D,C 绕一圈(否则是自交的蝴蝶结)
		const cyc = [0, 1, 3, 2];
		while (i < solid.length) {
			const n = solid[i++];
			if (!(n >= 3) || i + n * 6 > solid.length) break;
			const frontRaw = [];
			const backRaw = [];
			for (let k = 0; k < n; k++) {
				const p = [solid[i], solid[i + 1], solid[i + 2]];
				i += 3;
				frontRaw.push(p);
				track(p);
			}
			for (let k = 0; k < n; k++) {
				const p = [solid[i], solid[i + 1], solid[i + 2]];
				i += 3;
				backRaw.push(p);
				track(p);
			}
			const order = n === 4 ? cyc : [0, 1, 2];
			const front = order.map((k) => frontRaw[k]);
			const back = order.map((k) => backRaw[k]);
			faces.push(front);
			faces.push(back.slice().reverse());
			for (let k = 0; k < front.length; k++) {
				const j = (k + 1) % front.length;
				faces.push([front[k], front[j], back[j], back[k]]);
			}
			count++;
		}
		const cx = (minX + maxX) * 0.5;
		const cy = (minY + maxY) * 0.5;
		const cz = (minZ + maxZ) * 0.5;
		const size = Math.max(maxX - minX, maxY - minY, maxZ - minZ, 1e-3);
		state.geo3d = {
			faces: faces, count: count, total: res.parts, cx: cx, cy: cy, cz: cz, size: size,
			isSurface: res.placement === 'surface', truncated: !!res.solidTruncated,
		};
	}

	function draw3D() {
		const canvas = $('c3d');
		if (!canvas) return;
		const wrap = canvas.parentElement;
		const dpr = window.devicePixelRatio || 1;
		const W = Math.max(2, Math.floor(wrap.clientWidth * dpr));
		const H = Math.max(2, Math.floor(wrap.clientHeight * dpr));
		if (canvas.width !== W) canvas.width = W;
		if (canvas.height !== H) canvas.height = H;
		const ctx = canvas.getContext('2d');
		ctx.fillStyle = '#12151a';
		ctx.fillRect(0, 0, W, H);

		state.cpPx = [];
		state.gizmoPx = [];

		const geo = state.geo3d;
		if (!geo) {
			state.proj = null;
			ctx.fillStyle = '#98a3b3';
			ctx.font = (13 * dpr) + 'px "Segoe UI", "Microsoft YaHei", sans-serif';
			ctx.fillText('还没有生成内容', 20 * dpr, 32 * dpr);
			return;
		}

		const v = state.v3;
		const cyaw = Math.cos(v.yaw), syaw = Math.sin(v.yaw);
		const cpit = Math.cos(v.pitch), spit = Math.sin(v.pitch);
		const scale = (Math.min(W, H) * 0.82 / geo.size) * v.zoom;
		const camDist = geo.size * 3.0;
		const ox = W * 0.5;
		const oy = H * 0.5;

		const project = (p) => {
			const x = p[0] - geo.cx;
			const y = p[1] - geo.cy;
			const z = p[2] - geo.cz;
			const x1 = x * cyaw + z * syaw;
			const z1 = -x * syaw + z * cyaw;
			const y2 = y * cpit - z1 * spit;
			const z2 = y * spit + z1 * cpit;
			const f = camDist / Math.max(camDist + z2, camDist * 0.15);
			return [ox + x1 * scale * f, oy - y2 * scale * f, z2];
		};
		state.proj = project;

		// 曲面网格(本地按当前控制点求值，拖动时即时更新)
		if (geo.isSurface) {
			const cpArr = cpWorldArray();
			const gs = 12;
			const pts = [];
			for (let j = 0; j < gs; j++) {
				for (let i = 0; i < gs; i++) {
					pts.push(project(patchPoint(cpArr, i / (gs - 1), j / (gs - 1))));
				}
			}
			ctx.strokeStyle = 'rgba(79,156,249,0.40)';
			ctx.lineWidth = Math.max(1, dpr * 0.7);
			ctx.beginPath();
			for (let j = 0; j < gs; j++) {
				for (let i = 0; i < gs; i++) {
					const p = pts[j * gs + i];
					if (i === 0) ctx.moveTo(p[0], p[1]);
					else ctx.lineTo(p[0], p[1]);
				}
			}
			for (let i = 0; i < gs; i++) {
				for (let j = 0; j < gs; j++) {
					const p = pts[j * gs + i];
					if (j === 0) ctx.moveTo(p[0], p[1]);
					else ctx.lineTo(p[0], p[1]);
				}
			}
			ctx.stroke();
		}

		const light = [0.45, 0.62, 0.65];
		const ln = Math.hypot(light[0], light[1], light[2]);
		light[0] /= ln; light[1] /= ln; light[2] /= ln;
		const base = [0x6f, 0x72, 0x75];

		const list = [];
		for (const face of geo.faces) {
			const pts = face.map(project);
			let zsum = 0;
			for (const q of pts) zsum += q[2];
			const nz = zsum / pts.length;
			const p0 = pts[0], p1 = pts[1], p2 = pts[2];
			let nx = (p1[1] - p0[1]) * (p2[2] - p0[2]) - (p1[2] - p0[2]) * (p2[1] - p0[1]);
			let ny = (p1[2] - p0[2]) * (p2[0] - p0[0]) - (p1[0] - p0[0]) * (p2[2] - p0[2]);
			let nzz = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
			const nl = Math.hypot(nx, ny, nzz) || 1;
			nx /= nl; ny /= nl; nzz /= nl;
			const d = Math.abs(nx * light[0] + ny * light[1] + nzz * light[2]);
			list.push({ pts: pts, z: nz, shade: 0.32 + 0.68 * d });
		}
		list.sort((a, b) => a.z - b.z);
		for (const item of list) {
			ctx.beginPath();
			ctx.moveTo(item.pts[0][0], item.pts[0][1]);
			for (let i = 1; i < item.pts.length; i++) ctx.lineTo(item.pts[i][0], item.pts[i][1]);
			ctx.closePath();
			ctx.fillStyle = 'rgb(' + Math.round(base[0] * item.shade) + ',' +
				Math.round(base[1] * item.shade) + ',' + Math.round(base[2] * item.shade) + ')';
			ctx.fill();
		}

		// 控制点 + 轴柄
		if (geo.isSurface) drawControlPoints(ctx, project, dpr, W, H);

		ctx.fillStyle = 'rgba(231,236,243,0.75)';
		ctx.font = (12 * dpr) + 'px "Segoe UI", "Microsoft YaHei", sans-serif';
		let note = (geo.isSurface ? '曲面贴合 · ' : '平面 · ') +
			(geo.count < geo.total ? '仅预览前 ' + geo.count + ' / ' : '') + geo.total + ' 个零件';
		if (geo.truncated) note += '（3D 数据已截断）';
		ctx.fillText(note, 14 * dpr, H - 12 * dpr);
	}

	function drawControlPoints(ctx, project, dpr, W, H) {
		const cpArr = cpWorldArray();
		const r = 5.5 * dpr;               // 点半径(比原来大)
		const selR = 7.5 * dpr;
		state.cpPx = [];
		for (let i = 0; i < 16; i++) {
			const p = project(cpArr[i]);
			state.cpPx.push(p);
			const sel = (i === state.selCp);
			ctx.beginPath();
			ctx.arc(p[0], p[1], sel ? selR : r, 0, Math.PI * 2);
			ctx.fillStyle = sel ? '#e8d33c' : 'rgba(232,211,60,0.62)';
			ctx.fill();
			ctx.lineWidth = Math.max(1, (sel ? 2.2 : 1.2) * dpr);
			ctx.strokeStyle = sel ? '#ffffff' : 'rgba(0,0,0,0.55)';
			ctx.stroke();
		}
		// 选中点的三轴手柄
		if (state.selCp < 0) return;
		const p = cpArr[state.selCp];
		const geo = state.geo3d;
		const len = geo.size * 0.16;
		const head = 5 * dpr;
		for (let axis = 0; axis < 3; axis++) {
			const dir = [0, 0, 0];
			dir[axis] = len;
			const end = [p[0] + dir[0], p[1] + dir[1], p[2] + dir[2]];
			const a = project(p);
			const b = project(end);
			ctx.strokeStyle = AXIS_COLOR[axis];
			ctx.lineWidth = Math.max(1.5, 2.2 * dpr);
			ctx.beginPath();
			ctx.moveTo(a[0], a[1]);
			ctx.lineTo(b[0], b[1]);
			ctx.stroke();
			// 端点方块
			ctx.fillStyle = AXIS_COLOR[axis];
			ctx.fillRect(b[0] - head * 0.5, b[1] - head * 0.5, head, head);
			// 轴标签
			ctx.fillStyle = AXIS_COLOR[axis];
			ctx.font = (11 * dpr) + 'px "Segoe UI", "Microsoft YaHei", sans-serif';
			ctx.fillText('XYZ'[axis], b[0] + head, b[1] - head * 0.5);
			state.gizmoPx.push({ axis: axis, a: a, b: b });
		}
	}

	// ---------------- 3D 交互 ----------------

	function distToSegment(px, py, x1, y1, x2, y2) {
		const dx = x2 - x1, dy = y2 - y1;
		const len2 = dx * dx + dy * dy;
		if (len2 <= 0) return Math.hypot(px - x1, py - y1);
		let t = ((px - x1) * dx + (py - y1) * dy) / len2;
		t = clamp(t, 0, 1);
		return Math.hypot(px - (x1 + dx * t), py - (y1 + dy * t));
	}

	function setDragHint() {
		const v = state.v3;
		$('dragHint').textContent = v.dragMode
			? '旋转中：移动鼠标旋转，再点一下画面退出'
			: '点一下画面进入旋转（再点一次退出）· 滚轮缩放';
		$('c3d').style.cursor = v.dragMode ? 'move' : 'grab';
	}

	function pointerPos(e) {
		const rect = $('c3d').getBoundingClientRect();
		const dpr = window.devicePixelRatio || 1;
		return [(e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr];
	}

	function hitControlPoint(mx, my, dpr) {
		let best = -1;
		let bestD = 15 * dpr;
		state.cpPx.forEach((p, i) => {
			const d = Math.hypot(p[0] - mx, p[1] - my);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		});
		return best;
	}

	function hitGizmo(mx, my, dpr) {
		let best = -1;
		let bestD = 9 * dpr;
		for (const g of state.gizmoPx) {
			const d = distToSegment(mx, my, g.a[0], g.a[1], g.b[0], g.b[1]);
			if (d < bestD) {
				bestD = d;
				best = g.axis;
			}
		}
		return best;
	}

	function startAxisDrag(axis, mx, my) {
		const idx = state.selCp;
		if (idx < 0 || !state.proj) return;
		const p = cpWorld(idx);
		const dir = [0, 0, 0];
		dir[axis] = 1;
		const s0 = state.proj(p);
		const s1 = state.proj([p[0] + dir[0], p[1] + dir[1], p[2] + dir[2]]);
		const dx = s1[0] - s0[0];
		const dy = s1[1] - s0[1];
		const lenPx = Math.hypot(dx, dy);
		if (!(lenPx > 1e-6)) return;
		state.axisDrag = {
			axis: axis,
			idx: idx,
			dirPx: [dx / lenPx, dy / lenPx],
			pxPerWorld: lenPx,
			startMouse: [mx, my],
			startValue: state.cp[idx][axis],
		};
		$('axisHint').textContent = '拖动 ' + 'XYZ'[axis] + ' 轴中…';
	}

	function updateAxisDrag(mx, my) {
		const d = state.axisDrag;
		if (!d) return;
		const dpx = (mx - d.startMouse[0]) * d.dirPx[0] + (my - d.startMouse[1]) * d.dirPx[1];
		const worldDelta = dpx / d.pxPerWorld;           // 米
		let val = d.startValue + worldDelta / unitScale();
		if ($('snapOn').checked) {
			const step = Math.abs(parseFloat($('snapStep').value)) || 0.05;
			val = Math.round(val / step) * step;
		}
		state.cp[d.idx][d.axis] = val;
		state.cpEdited = true;
		syncCpInputs();
		draw3D();
		scheduleGenerate(180);
	}

	function endAxisDrag() {
		state.axisDrag = null;
		$('axisHint').textContent = '';
		clearTimeout(debounceTimer);
		generate();
	}

	function init3DControls() {
		const canvas = $('c3d');
		const v = state.v3;
		setDragHint();

		canvas.addEventListener('pointerdown', (e) => {
			const [mx, my] = pointerPos(e);
			const dpr = window.devicePixelRatio || 1;
			// 1) 先看是否点到轴柄
			const axis = hitGizmo(mx, my, dpr);
			if (axis >= 0) {
				startAxisDrag(axis, mx, my);
				return;
			}
			// 2) 再看点
			const idx = hitControlPoint(mx, my, dpr);
			if (idx >= 0) {
				selectCp(idx);
				return;
			}
			// 3) 否则切换"旋转模式"（点一下进入 / 再点一下退出）
			v.dragMode = !v.dragMode;
			v.lx = mx;
			v.ly = my;
			setDragHint();
		});

		canvas.addEventListener('pointermove', (e) => {
			const [mx, my] = pointerPos(e);
			const dpr = window.devicePixelRatio || 1;
			if (state.axisDrag) {
				updateAxisDrag(mx, my);
				return;
			}
			if (v.dragMode) {
				if (v.anchor) {          // 刚从画布外移回来，只重新锚定，不产生跳动
					v.lx = mx;
					v.ly = my;
					v.anchor = false;
					return;
				}
				v.yaw += (mx - v.lx) * 0.008;
				v.pitch = clamp(v.pitch + (my - v.ly) * 0.008, -1.45, 1.45);
				v.lx = mx;
				v.ly = my;
				draw3D();
				return;
			}
			// 悬停提示
			const hoverAxis = hitGizmo(mx, my, dpr);
			const hoverPt = hitControlPoint(mx, my, dpr);
			canvas.style.cursor = hoverAxis >= 0 ? 'crosshair' : (hoverPt >= 0 ? 'pointer' : 'grab');
		});

		canvas.addEventListener('pointerleave', () => {
			state.v3.anchor = true;
		});

		const up = () => {
			if (state.axisDrag) endAxisDrag();
		};
		canvas.addEventListener('pointerup', up);
		window.addEventListener('pointerup', up);

		canvas.addEventListener('wheel', (e) => {
			e.preventDefault();
			v.zoom = clamp(v.zoom * (e.deltaY > 0 ? 0.92 : 1.08), 0.2, 6);
			draw3D();
		}, { passive: false });

		$('reset3d').addEventListener('click', () => {
			v.yaw = 0.62;
			v.pitch = 0.28;
			v.zoom = 1;
			v.dragMode = false;
			setDragHint();
			draw3D();
		});
	}

	// ---------------- 统计信息 ----------------

	function updateInfo(json) {
		const s = json.stats;
		const b = json.bounds;
		const tris = s.parts - s.quads;
		const lines = [];
		lines.push('字体        ' + (json.family ? json.family + ' ' + json.style + ' — ' : '') + json.font);
		lines.push('文本        ' + json.text);
		lines.push('字号        ' + json.size + ' m（1 em = ' + json.size + ' m）');
		lines.push('曲线容差    ' + json.tolerance + ' em（质量等级换算）');
		lines.push('零件厚度    ' + json.thickness + ' m（x = ±' + (json.thickness / 2).toFixed(3) + ' m）');
		lines.push('放置方式    ' + (json.placement === 'surface'
			? '贝塞尔曲面（3D 标签页查看贴合效果）' : '平面'));
		lines.push('');
		lines.push('字符数      ' + s.codepoints + '（有轮廓 ' + s.glyphs + '，空白 ' + s.blanks + '）');
		lines.push('轮廓环      ' + s.rings);
		lines.push('实心区域    ' + s.regions);
		lines.push('孔洞        ' + s.holes + (s.holesDropped ? '（丢弃 ' + s.holesDropped + '）' : ''));
		lines.push('');
		lines.push('三角面(优化前) ' + s.trianglesIn);
		lines.push('保角翻转    ' + s.flips + ' 次');
		lines.push('零件(优化后)   ' + s.parts + '（四边形 ' + s.quads + '，三角 ' + tris + '）' +
			(json.truncated ? '（已截断）' : ''));
		lines.push('细长面(<5°)    ' + s.sliversIn + ' -> ' + s.sliversOut);
		lines.push('  其中大细长面 ' + s.bigThinIn + ' -> ' + s.bigThinOut +
			'，占总面积 ' + s.bigThinShareIn.toFixed(2) + '% -> ' + s.bigThinShareOut.toFixed(2) + '%');
		lines.push('最小角      ' + s.minAngleIn.toFixed(2) + '° -> ' + s.minAngleOut.toFixed(2) + '°');
		lines.push('');
		lines.push('包围盒      x ' + b.minX.toFixed(3) + ' … ' + b.maxX.toFixed(3) + ' m');
		lines.push('            y ' + b.minY.toFixed(3) + ' … ' + b.maxY.toFixed(3) + ' m');
		lines.push('几何耗时    ' + s.geometryMs + ' ms');
		lines.push('请求耗时    ' + json.requestMs + ' ms');
		lines.push('输出文件    ' + (json.written || '（未写入）'));
		if ((json.warnings || []).length) {
			lines.push('');
			lines.push('提示        ' + json.warnings.join('\n            '));
		}
		$('info').textContent = lines.join('\n');
		updateSurfaceVisibility();
		$('flatHint').textContent = json.placement === 'surface' ? '曲面模式：此处显示未贴合的平面字形' : '';
		$('stats').textContent = '字符 ' + s.codepoints + ' · 轮廓环 ' + s.rings + ' · 孔洞 ' + s.holes +
			' · 三角面 ' + s.trianglesIn + ' → 零件 ' + s.parts + '（四边形 ' + s.quads + '）' +
			' · 大细长面 ' + s.bigThinIn + '→' + s.bigThinOut +
			'（面积 ' + s.bigThinShareOut.toFixed(2) + '%）' +
			' · 几何 ' + s.geometryMs + ' ms' + (json.written ? ' · 已写入 ' + json.written : '');
	}

	// ---------------- 事件绑定 ----------------

	function initTabs() {
		const buttons = document.querySelectorAll('.tabs button');
		buttons.forEach((btn) => {
			btn.addEventListener('click', () => {
				buttons.forEach((b) => b.classList.toggle('active', b === btn));
				document.querySelectorAll('.tab').forEach((t) => {
					t.classList.toggle('active', t.id === btn.dataset.tab);
				});
				if (btn.dataset.tab === 'tab-2d') draw2D();
				if (btn.dataset.tab === 'tab-3d') draw3D();
			});
		});
	}

	function initInputs() {
		$('size').addEventListener('input', () => {
			$('sizeOut').textContent = parseFloat($('size').value).toFixed(1) + ' m';
			scheduleGenerate();
		});
		$('quality').addEventListener('input', () => {
			$('qualityOut').textContent = $('quality').value;
			scheduleGenerate();
		});
		$('thickness').addEventListener('input', () => {
			$('thickOut').textContent = parseFloat($('thickness').value).toFixed(2) + ' m';
			scheduleGenerate();
		});
		$('text').addEventListener('input', () => scheduleGenerate());
		$('out').addEventListener('input', () => scheduleGenerate(600));
		$('font').addEventListener('change', () => {
			$('fontPath').value = '';
			scheduleGenerate(0);
		});
		$('fontPath').addEventListener('change', () => scheduleGenerate(0));
		$('kerning').addEventListener('change', () => scheduleGenerate(0));
		$('quads').addEventListener('change', () => scheduleGenerate(0));
		$('autowrite').addEventListener('change', () => scheduleGenerate(0));
		$('flipNormal').addEventListener('change', () => scheduleGenerate(0));

		// 放置方式
		$('placement').addEventListener('change', () => {
			updateSurfaceVisibility();
			if ($('placement').value === 'surface') {
				const btn = document.querySelector('.tabs button[data-tab="tab-3d"]');
				if (btn) btn.click();
			}
			scheduleGenerate(0);
		});

		// 归一化 / 实测
		document.querySelectorAll('#sunit button').forEach((btn) => {
			btn.addEventListener('click', () => setUnit(btn.dataset.unit));
		});

		// 预设
		document.querySelectorAll('.preset-row button').forEach((btn) => {
			btn.addEventListener('click', () => setPreset(btn.dataset.preset));
		});

		// 控制点坐标输入
		['cpx', 'cpy', 'cpz'].forEach((id) => {
			$(id).addEventListener('input', applyCpInputs);
			$(id).addEventListener('blur', () => syncCpInputs(true));   // 输入完再规整显示
		});

		$('generate').addEventListener('click', () => {
			clearTimeout(debounceTimer);
			generate();
		});
		['showTri', 'showRef', 'showEdges'].forEach((id) => {
			$(id).addEventListener('change', draw2D);
		});
		window.addEventListener('resize', () => {
			clearTimeout(window.__rsz);
			window.__rsz = setTimeout(() => {
				draw2D();
				draw3D();
			}, 120);
		});
	}

	async function boot() {
		initTabs();
		init3DControls();
		initInputs();
		$('sizeOut').textContent = parseFloat($('size').value).toFixed(1) + ' m';
		$('qualityOut').textContent = $('quality').value;
		$('thickOut').textContent = parseFloat($('thickness').value).toFixed(2) + ' m';
		renderCpGrid();
		syncCpInputs();
		await loadFonts();
		try {
			const ping = await (await fetch('/api/ping')).json();
			if (ping.version) $('ver').textContent = 'v' + ping.version;
		} catch (_) { /* 忽略 */ }
		await generate();
	}

	boot();
})();
