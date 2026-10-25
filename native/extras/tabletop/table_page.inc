R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Deckboy table</title>
<link rel="stylesheet" href="/remote.css">
<style>
.map{position:relative;box-sizing:content-box;aspect-ratio:30/22;background:#000;border:4px solid var(--deep,#0f380f);margin-bottom:6px;overflow:hidden}
.map .geometry{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.map button{position:absolute;width:52px;height:52px;margin:-26px 0 0 -26px;border-radius:50%;
 background:transparent;color:#fff;border:0;box-shadow:none;text-shadow:0 1px 2px #000;min-height:0;padding:0;font:700 18px var(--read,sans-serif)}
.map button.p1{color:#ff9f94}.map button.p2{color:#9fc0ff}
.map button.hl{background:rgba(255,255,255,.18)}
.map button.place{width:34px;height:34px;margin:-17px 0 0 -17px;border-radius:0;transform:rotate(45deg);border:3px solid #e7d36a;color:#e7d36a;font-size:12px}
.map .dot{position:absolute;width:10px;height:10px;margin:-5px 0 0 -5px;border-radius:50%;background:#fff0a0;pointer-events:none}
.map.measuring{outline:3px solid #fff0a0;cursor:crosshair}
.tac{border:4px solid var(--deep,#0f380f);padding:8px;margin-bottom:8px}
.tac .who{font-family:var(--px);font-size:.6rem;margin:0 0 6px}
.tac .cond{font-size:.85rem;margin:6px 0;line-height:1.3}
.tac .row2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;margin-bottom:6px}
.tac .row3 button{font-size:.5rem;min-height:52px}
.manual p{font-size:.85rem;margin:8px 0 4px;line-height:1.3}
.row4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;margin-bottom:6px}
.grid3 button,.row3 button,.row4 button{font-size:.55rem;padding:.3rem .15rem}
.elements{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}
.elements button{font-size:.55rem;line-height:1.5;overflow-wrap:anywhere}
.plan-group{border-top:2px solid var(--deep,#0f380f);margin-top:18px;padding-top:6px}
.map button.p3{color:#ddd;outline:3px dashed #ddd;outline-offset:-6px}
.sec{border:4px solid var(--deep,#0f380f);padding:8px;margin-bottom:8px}
.sec .who{font-family:var(--px);font-size:.6rem;margin:0 0 6px}
.sec .card{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:6px;align-items:center;margin-bottom:6px}
.sec .card span{font-size:.85rem;overflow-wrap:anywhere}
.setup select{width:100%;min-height:44px;margin-bottom:6px;font:inherit}
</style></head><body>
<main style="max-width:520px;margin:0 auto;padding:12px">
<header class="top"><span class="plate">DECKBOY</span><span id="link" class="link">TABLE</span>
<a class="small" href="/remote" style="text-decoration:none"><button class="small">DECKS</button></a></header>
<p id="pair" class="hint" hidden>Pair this phone first: open <a href="/remote">the controller</a> and scan the desk's code.</p>
<div class="now"><div class="now-top"><span id="round">ROUND -</span><span id="vp">VP -</span></div>
<div id="turn" class="now-name">-</div><div class="now-next">PLAN: <span id="plan">-</span></div></div>
<p id="boardnote" class="hint" hidden></p>
<div class="row3"><button data-c="TABLE ROUND -">ROUND -</button><button class="on" data-c="TABLE TURN NEXT">NEXT TURN</button><button data-c="TABLE ROUND +">ROUND +</button></div>
<div class="row3" style="margin-top:6px"><button data-c="TABLE PHASE BACK">&lt; PHASE</button><button id="phase" disabled>PHASE</button><button class="on" data-c="TABLE PHASE NEXT">NEXT PHASE &gt;</button></div>
<div class="row4" style="margin-top:6px"><button data-c="TABLE VP P1 -" id="v1m">P1 -</button><button data-c="TABLE VP P1 +" id="v1p">P1 +</button><button data-c="TABLE VP P2 -" id="v2m">P2 -</button><button data-c="TABLE VP P2 +" id="v2p">P2 +</button></div>
<div class="row4" style="margin-top:6px"><button data-c="TABLE CP P1 -" id="c1m">CP -</button><button data-c="TABLE CP P1 +" id="c1p">CP +</button><button data-c="TABLE CP P2 -" id="c2m">CP -</button><button data-c="TABLE CP P2 +" id="c2p">CP +</button></div>
<div class="row3" style="margin-top:6px"><button id="clock" data-c="TABLE CLOCK TOGGLE">CLOCK</button><button id="clockpause" data-c="TABLE CLOCK PAUSE">PAUSE</button><button data-c="TABLE CLOCK RESET">RESET CLOCKS</button></div>
<p class="label">OBJECTIVES <span class="dim">TAP: WHO HOLDS IT &middot; HOLD: HIGHLIGHT &middot; &#9671; PLACES OF POWER</span></p>
<div id="map" class="map"><svg class="geometry" aria-hidden="true"></svg></div>
<section id="w40k" hidden>
<p class="label">SECONDARIES <span class="dim">TYPE A CARD'S NAME; SCORE ASKS ITS POINTS</span></p>
<div id="sec"></div>
<p class="label">MISSION SETUP <span class="dim">FROM YOUR OWN CARDS</span></p>
<div class="setup">
<label class="small">DEPLOYMENT<select id="m-dep"></select></label>
<label class="small">PRIMARY<select id="m-pri"></select></label>
<label class="small">MISSION RULE<select id="m-rule"></select></label>
<button id="m-go" class="on">SET UP THIS MISSION</button>
<div class="row2" style="display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;margin-top:6px"><button id="m-setup" data-c="TABLE SETUP ON">SETUP VIEW</button><button id="m-battle" data-c="TABLE SETUP OFF">BATTLE</button></div>
<label class="small" style="margin-top:8px;display:block">TERRAIN LAYOUT<select id="m-layout"></select></label>
<p id="m-hint" class="hint"></p>
</div>
</section>
<p class="label">ELEMENTS <span class="dim">MUTE / RESTORE</span></p>
<p class="hint">Muted elements disappear. Muted objectives do not count towards automatic scoring. Restore them here when they become active again.</p>
<div id="elements" class="elements"></div>
<button data-c="TABLE MUTE ALL OFF" style="margin-top:6px">RESTORE ALL ELEMENTS</button>
<p class="label">SCORING <span id="scoringhint" class="dim">AUTOMATIC AT THE END OF EACH TURN</span></p>
<div class="row3"><button id="autoscore" data-c="TABLE SCORE AUTO TOGGLE">AUTO-SCORE</button><button id="lastscore" disabled style="grid-column:span 2">-</button></div>
<div id="manual" class="manual"></div>
<p id="tacticslabel" class="label">BATTLE TACTICS</p>
<div id="tactics"></div>
<p class="label">MEASURE <span class="dim" id="measurehint">PICK ONE, THEN TAP THE MAP</span></p>
<div class="row4">
<button data-measure="RINGS">RINGS</button><button data-measure="RULER">RULER</button><button data-measure="MOVE">MOVE</button><button data-c="TABLE MEASURE CLEAR">CLEAR</button>
</div>
<div class="row4" style="margin-top:6px"><button data-measure="DS">9" NO DS</button><button data-measure="R6">6"</button><button data-measure="R12">12"</button><button data-measure="R24">24"</button></div>
<div class="row4" style="margin-top:6px"><button id="movem">MOVE -</button><button id="moveval" disabled>MOVE 5"</button><button id="movep">MOVE +</button><button id="ringset">RINGS 3-12</button></div>
<p class="label">LIGHT UP</p>
<div class="grid3">
<button data-l="territories">TERRITORIES</button><button data-l="deployment">DEPLOYMENT</button><button data-l="terrain">TERRAIN</button>
<button data-l="places">PLACES</button><button data-l="objectives">OBJECTIVES</button><button data-l="strip">ROUND STRIP</button><button data-l="edge">PHASE EDGE</button>
<button data-l="guides">BOARD LINES</button>
<button id="anim">ANIMATE</button><button id="cal">CALIBRATE</button><button data-c="TABLE LAYER ALL">ALL ON</button>
</div>
<p class="label">GROUND</p>
<div class="grid3">
<button data-ground="none">BARE</button><button data-ground="grass">GRASS</button><button data-ground="swamp">SWAMP</button>
<button data-ground="ash">ASH</button><button data-ground="lava">LAVA</button><button data-ground="snow">SNOW</button>
<button data-ground="sand">SAND</button><button data-c="TABLE GROUND -">FAINTER</button><button data-c="TABLE GROUND +">STRONGER</button>
</div>
<p class="label">LOOK</p>
<div class="grid3">
<button data-style="0">CLEAN</button><button data-style="1">ARCANE</button><button data-style="2">EMBER</button>
<button data-c="TABLE SPEED -">SLOWER</button><button id="speed" disabled>SPEED</button><button data-c="TABLE SPEED +">FASTER</button>
<button data-c="TABLE BRIGHT -">DIMMER</button><button id="bright" disabled>BRIGHT</button><button data-c="TABLE BRIGHT +">BRIGHTER</button>
<button data-l="labels">LABELS</button><button data-c="TABLE SPEED 0">FREEZE</button><button data-c="TABLE SPEED 100">NORMAL</button>
</div>
<div class="row4" style="margin-top:6px">
<label class="small">ATTACKER<input type="color" id="c1" data-who="P1"></label>
<label class="small">DEFENDER<input type="color" id="c2" data-who="P2"></label>
<label class="small">HIGHLIGHT<input type="color" id="c3" data-who="HIGHLIGHT"></label>
<label class="small">PLACES<input type="color" id="c4" data-who="PLACE"></label>
</div>
<div id="plans">
<section class="plan-group" id="pitched-group"><p class="label">PITCHED BATTLE</p>
<div class="row3"><button data-c="TABLE BOARD 60X44" id="b60">60" X 44"</button><button data-c="TABLE BOARD 44X30" id="b44">44" X 30"</button></div>
<div id="pitched-plans" class="chips outs" style="flex-wrap:wrap"></div></section>
<section class="plan-group" id="w40k-group"><p class="label">40K</p>
<div id="w40k-plans" class="chips outs" style="flex-wrap:wrap"></div></section>
<section class="plan-group" id="spearhead-group"><p class="label">SPEARHEAD &middot; 30&quot; X 22&quot;</p>
<div id="spearhead-plans"></div></section>
<section class="plan-group" id="custom-group" hidden><p class="label">CUSTOM PLANS</p>
<div id="custom-plans" class="chips outs" style="flex-wrap:wrap"></div></section>
</div>
<button data-c="TABLE RESET" id="reset" style="margin-top:18px">NEW GAME</button>
<p id="result" class="result"></p>
</main>
<script src="/remote/x/table.js"></script></body></html>)PAGE"
