// =============================================================================
//  web_page.h -- the machine's web UI, as one PROGMEM string
// =============================================================================
//
//  WHY THIS IS A SEPARATE FILE
//
//  The Arduino build does not compile your .ino directly. It preprocesses it
//  first: concatenating files, inserting #include <Arduino.h>, scanning for
//  function definitions and injecting generated prototypes. That scanner does
//  not reliably understand C++11 raw string literals, and on a literal this
//  large it can cut the string short -- after which the browser code inside it
//  gets handed to the C++ compiler, which reports something like:
//
//      expected constructor, destructor, or type conversion before '(' token
//
//  pointing at a line of minified JavaScript.
//
//  Arduino does NOT preprocess .h files. Moving the page here takes it out of
//  reach of that step entirely. Keep it in this file; do not paste it back
//  into the .ino.
//
//  This is still one sketch. Put web_page.h next to the .ino in the same
//  folder and it compiles and flashes as a single click, exactly as before.
// =============================================================================

#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"STRINGARTPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>String Art Studio</title>
<style>
  :root{
    --bg:#f4f1ea; --panel:#ffffff; --panel-2:#ece6d7;
    --text:#221d17; --text-dim:#6f6355; --border:#ddd3bd;
    --copper:#a85a1c; --copper-soft:#ecd9c2;
    --signal:#2f5d8a; --signal-soft:#dbe6ee;
    --good:#3f7d4f; --good-soft:#dde8d5;
    --warn:#a3402f; --warn-soft:#f2ddd6;
    --canvas-bg:#ffffff;
    --shadow:0 1px 2px rgba(30,20,10,0.06), 0 6px 20px -14px rgba(30,20,10,0.3);
  }
  @media (prefers-color-scheme: dark){
    :root{
      --bg:#161209; --panel:#1f1a12; --panel-2:#261f15;
      --text:#efe7d8; --text-dim:#a89a80; --border:#3c3221;
      --copper:#e08a3e; --copper-soft:#3a2a15;
      --signal:#7fabd6; --signal-soft:#1d2b38;
      --good:#71bd7d; --good-soft:#1c2e1e;
      --warn:#e08272; --warn-soft:#3a2119;
      --shadow:0 1px 2px rgba(0,0,0,0.4), 0 10px 30px -18px rgba(0,0,0,0.7);
    }
  }
  *{box-sizing:border-box}
  html,body{height:100%}
  body{
    margin:0; background:var(--bg); color:var(--text);
    font-family:"IBM Plex Sans",system-ui,sans-serif; line-height:1.45;
    display:flex; flex-direction:column;
  }
  a{color:var(--signal)}

  .topbar{
    display:flex; align-items:baseline; gap:10px; padding:14px 20px;
    border-bottom:1px solid var(--border); flex:none;
  }
  .topbar h1{font-size:1.05rem; margin:0}
  .topbar .eyebrow{font-size:.72rem; letter-spacing:.06em; text-transform:uppercase; color:var(--copper); font-weight:600}

  .layout{ display:flex; flex:1; min-height:0; }

  .stage{
    flex:1; min-width:0; display:flex; align-items:center; justify-content:center;
    padding:24px; overflow:auto; background:
      radial-gradient(circle at 50% 40%, var(--panel-2) 0%, var(--bg) 70%);
  }
  #canvasWrap{
    background:var(--canvas-bg); border-radius:14px; box-shadow:var(--shadow);
    padding:16px; max-width:100%;
  }
  #artCanvas{ display:block; max-width:100%; height:auto; border-radius:6px; }
  #dropHint{
    position:absolute; color:var(--text-dim); font-size:.9rem; text-align:center;
    pointer-events:none;
  }
  #canvasOuter{ position:relative; display:flex; align-items:center; justify-content:center; max-width:100%; }

  .sidebar{
    width:360px; flex:none; border-left:1px solid var(--border); background:var(--panel);
    padding:20px; overflow-y:auto;
  }

  /* ---- Mobile / narrow viewport ---------------------------------------
     On desktop the stage (image) and sidebar (controls) sit side-by-side,
     each independently scrollable within a viewport-height app shell. On
     phones that nested-scrolling layout backfires: the sidebar's full,
     un-shrinkable content height starves the image area of room, so the
     canvas collapses to a sliver a few pixels tall inside its own tiny
     scrollbox -- which is exactly what makes the selected photo and the
     generated art look "invisible" on a phone. The fix is to stop trying
     to fit everything inside one screen and let the page scroll normally
     instead, like any other mobile web page. */
  @media (max-width:820px){
    html, body{ height:auto; min-height:100%; }
    body{ overflow-x:hidden; }
    .layout{ flex-direction:column; flex:none; height:auto; min-height:0; overflow:visible; }
    .stage{
      flex:none; overflow:visible; padding:16px 16px 8px;
      min-height:min(88vw, 420px);
    }
    #canvasWrap{ width:100%; max-width:480px; margin:0 auto; }
    .sidebar{
      width:auto; flex:none; border-left:none; border-top:1px solid var(--border);
      overflow-y:visible; max-height:none;
    }
  }
  @media (max-width:420px){
    .btn-pair{ flex-direction:column; }
  }

  .section-title{
    font-size:.72rem; letter-spacing:.06em; text-transform:uppercase; color:var(--copper);
    font-weight:700; margin:0 0 12px;
  }

  .field{ margin-bottom:16px; }
  .field label{
    display:flex; justify-content:space-between; align-items:baseline;
    font-size:.78rem; color:var(--text-dim); margin-bottom:5px; font-weight:600;
  }
  .field label .val{ font-family:"IBM Plex Mono",monospace; color:var(--text); font-weight:600; }
  input[type=number], input[type=text], textarea{
    width:100%; padding:8px 10px; border:1px solid var(--border); border-radius:7px;
    background:var(--bg); color:var(--text); font-family:"IBM Plex Mono",monospace; font-size:.9rem;
    box-sizing:border-box;
  }
  textarea{ resize:vertical; line-height:1.45; }
  input[type=range]{ width:100%; accent-color:var(--copper); }

  .fileBtn{
    display:block; width:100%; text-align:center; padding:10px; border:1px dashed var(--border);
    border-radius:8px; color:var(--text-dim); font-size:.85rem; cursor:pointer; background:var(--bg);
  }
  .fileBtn:hover{ border-color:var(--signal); color:var(--signal); }
  input[type=file]{ display:none; }

  .switch-row{ display:flex; align-items:center; justify-content:space-between; margin-bottom:16px; }
  .switch-row span{ font-size:.85rem; font-weight:600; }
  .switch{ position:relative; width:40px; height:22px; flex:none; }
  .switch input{ opacity:0; width:0; height:0; }
  .slider{
    position:absolute; inset:0; background:var(--border); border-radius:100px; cursor:pointer; transition:.15s;
  }
  .slider::before{
    content:""; position:absolute; width:16px; height:16px; left:3px; top:3px; background:#fff;
    border-radius:50%; transition:.15s;
  }
  .switch input:checked + .slider{ background:var(--copper); }
  .switch input:checked + .slider::before{ transform:translateX(18px); }

  hr{ border:none; border-top:1px solid var(--border); margin:20px 0; }

  button{
    font-family:"IBM Plex Sans",sans-serif; font-weight:600; font-size:.9rem;
    border:none; border-radius:8px; padding:11px 14px; cursor:pointer; width:100%;
  }
  button.primary{ background:var(--copper); color:#fff; margin-bottom:8px; }
  button.primary:disabled{ opacity:.5; cursor:not-allowed; }
  button.secondary{ background:var(--signal-soft); color:var(--signal); margin-bottom:8px; }
  button.secondary:disabled{ opacity:.45; cursor:not-allowed; }
  button.ghost{ background:transparent; border:1px solid var(--border); color:var(--text); }
  button.danger{ background:var(--warn-soft); color:var(--warn); }
  .btn-pair{ display:flex; gap:8px; }
  .btn-pair button{ margin-bottom:8px; }

  .pill{ display:inline-block; font-size:.7rem; font-weight:600; padding:3px 9px; border-radius:100px; }
  .pill.good{ background:var(--good-soft); color:var(--good); }
  .pill.warn{ background:var(--warn-soft); color:var(--warn); }
  .pill.idle{ background:var(--panel-2); color:var(--text-dim); }

  .progress-track{
    height:8px; background:var(--panel-2); border-radius:100px; overflow:hidden; margin:10px 0 6px;
  }
  .progress-fill{ height:100%; background:var(--copper); width:0%; transition:width .1s linear; }
  .progress-label{ font-size:.78rem; color:var(--text-dim); font-family:"IBM Plex Mono",monospace; }

  .msg{ font-size:.82rem; margin-top:8px; padding:8px 10px; border-radius:7px; display:none; }
  .msg.show{ display:block; }
  .msg.ok{ background:var(--good-soft); color:var(--good); }
  .msg.err{ background:var(--warn-soft); color:var(--warn); }

  .stat-line{ font-size:.78rem; color:var(--text-dim); font-family:"IBM Plex Mono",monospace; margin-top:4px; }

  /* Wrap section (indexer) */
  .nail-readout{ font-size:3.2rem; text-align:center; margin:4px 0; font-weight:700; font-family:"IBM Plex Mono",monospace; }
  .idx-sub{ text-align:center; color:var(--text-dim); font-size:.8rem; margin-bottom:10px; }
  progress[id^="idx"]{ width:100%; height:8px; margin-bottom:14px; }
  details{ margin-top:6px; }
  details summary{ cursor:pointer; font-size:.82rem; color:var(--text-dim); font-weight:600; margin-bottom:10px; }
  details .field{ margin-bottom:10px; }

  .eta-grid{
    display:grid; grid-template-columns:1fr 1fr; gap:6px 12px;
    margin:2px 0 14px; font-family:"IBM Plex Mono",monospace;
  }
  .eta-grid > div{ display:flex; flex-direction:column; }
  .eta-k{ font-size:.66rem; letter-spacing:.06em; text-transform:uppercase; color:var(--text-dim); }
  .eta-v{ font-size:.94rem; color:var(--text); }
  .jog-row{ display:flex; gap:5px; }
  .jog-row button{ flex:1; width:auto; padding:9px 0; margin:0; font-size:.82rem; }
  .resume-banner{
    font-size:.78rem; line-height:1.5; border-radius:8px; padding:9px 11px;
    border:1px solid var(--warn); border-left-width:3px;
    background:var(--bg); color:var(--text); margin-top:10px;
  }
  .resume-banner b{ display:block; margin-bottom:2px; }

  /* Base template designer */
  .sync-box{
    font-family:"IBM Plex Mono",monospace; font-size:.75rem; line-height:1.6;
    border:1px solid var(--border); border-left-width:3px; border-radius:7px;
    padding:9px 11px; margin-bottom:10px; background:var(--bg);
  }
  .sync-box.exact{ border-left-color:var(--good); }
  .sync-box.approx{ border-left-color:var(--warn); }
  .sync-box b{ color:var(--text); }
  .sync-box .dim{ color:var(--text-dim); }
  #basePreview{
    background:#fff; border:1px solid var(--border); border-radius:10px;
    padding:8px; margin-bottom:10px; overflow:hidden;
  }
  #basePreview svg{ display:block; width:100%; height:auto; }

  /* Crop modal */
  #cropOverlay{
    position:fixed; inset:0; background:rgba(10,8,4,.6); display:flex; align-items:center; justify-content:center;
    z-index:1000; padding:20px;
  }
  #cropBox{
    background:var(--panel); border-radius:12px; padding:18px; max-width:640px; width:100%;
    box-shadow:var(--shadow);
  }
  #cropBox h3{ margin:0 0 12px; font-size:1rem; }
  #cropImageWrap{ max-height:60vh; overflow:hidden; background:#000; border-radius:8px; }
  #cropImage{ display:block; max-width:100%; }
  .crop-actions{ display:flex; gap:10px; margin-top:14px; }
  .crop-actions button{ width:auto; flex:1; }
  [hidden]{ display:none !important; }

/* ---- vendored cropper.min.css ---- */
/*!
 * Cropper.js v1.6.1
 * https://fengyuanchen.github.io/cropperjs
 *
 * Copyright 2015-present Chen Fengyuan
 * Released under the MIT license
 *
 * Date: 2023-09-17T03:44:17.565Z
 */.cropper-container{direction:ltr;font-size:0;line-height:0;position:relative;-ms-touch-action:none;touch-action:none;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none}.cropper-container img{backface-visibility:hidden;display:block;height:100%;image-orientation:0deg;max-height:none!important;max-width:none!important;min-height:0!important;min-width:0!important;width:100%}.cropper-canvas,.cropper-crop-box,.cropper-drag-box,.cropper-modal,.cropper-wrap-box{bottom:0;left:0;position:absolute;right:0;top:0}.cropper-canvas,.cropper-wrap-box{overflow:hidden}.cropper-drag-box{background-color:#fff;opacity:0}.cropper-modal{background-color:#000;opacity:.5}.cropper-view-box{display:block;height:100%;outline:1px solid #39f;outline-color:rgba(51,153,255,.75);overflow:hidden;width:100%}.cropper-dashed{border:0 dashed #eee;display:block;opacity:.5;position:absolute}.cropper-dashed.dashed-h{border-bottom-width:1px;border-top-width:1px;height:33.33333%;left:0;top:33.33333%;width:100%}.cropper-dashed.dashed-v{border-left-width:1px;border-right-width:1px;height:100%;left:33.33333%;top:0;width:33.33333%}.cropper-center{display:block;height:0;left:50%;opacity:.75;position:absolute;top:50%;width:0}.cropper-center:after,.cropper-center:before{background-color:#eee;content:" ";display:block;position:absolute}.cropper-center:before{height:1px;left:-3px;top:0;width:7px}.cropper-center:after{height:7px;left:0;top:-3px;width:1px}.cropper-face,.cropper-line,.cropper-point{display:block;height:100%;opacity:.1;position:absolute;width:100%}.cropper-face{background-color:#fff;left:0;top:0}.cropper-line{background-color:#39f}.cropper-line.line-e{cursor:ew-resize;right:-3px;top:0;width:5px}.cropper-line.line-n{cursor:ns-resize;height:5px;left:0;top:-3px}.cropper-line.line-w{cursor:ew-resize;left:-3px;top:0;width:5px}.cropper-line.line-s{bottom:-3px;cursor:ns-resize;height:5px;left:0}.cropper-point{background-color:#39f;height:5px;opacity:.75;width:5px}.cropper-point.point-e{cursor:ew-resize;margin-top:-3px;right:-3px;top:50%}.cropper-point.point-n{cursor:ns-resize;left:50%;margin-left:-3px;top:-3px}.cropper-point.point-w{cursor:ew-resize;left:-3px;margin-top:-3px;top:50%}.cropper-point.point-s{bottom:-3px;cursor:s-resize;left:50%;margin-left:-3px}.cropper-point.point-ne{cursor:nesw-resize;right:-3px;top:-3px}.cropper-point.point-nw{cursor:nwse-resize;left:-3px;top:-3px}.cropper-point.point-sw{bottom:-3px;cursor:nesw-resize;left:-3px}.cropper-point.point-se{bottom:-3px;cursor:nwse-resize;height:20px;opacity:1;right:-3px;width:20px}@media (min-width:768px){.cropper-point.point-se{height:15px;width:15px}}@media (min-width:992px){.cropper-point.point-se{height:10px;width:10px}}@media (min-width:1200px){.cropper-point.point-se{height:5px;opacity:.75;width:5px}}.cropper-point.point-se:before{background-color:#39f;bottom:-50%;content:" ";display:block;height:200%;opacity:0;position:absolute;right:-50%;width:200%}.cropper-invisible{opacity:0}.cropper-bg{background-image:url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAQMAAAAlPW0iAAAAA3NCSVQICAjb4U/gAAAABlBMVEXMzMz////TjRV2AAAACXBIWXMAAArrAAAK6wGCiw1aAAAAHHRFWHRTb2Z0d2FyZQBBZG9iZSBGaXJld29ya3MgQ1M26LyyjAAAABFJREFUCJlj+M/AgBVhF/0PAH6/D/HkDxOGAAAAAElFTkSuQmCC")}.cropper-hide{display:block;height:0;position:absolute;width:0}.cropper-hidden{display:none!important}.cropper-move{cursor:move}.cropper-crop{cursor:crosshair}.cropper-disabled .cropper-drag-box,.cropper-disabled .cropper-face,.cropper-disabled .cropper-line,.cropper-disabled .cropper-point{cursor:not-allowed}
</style>
</head>
<body>

<div class="topbar">
  <span class="eyebrow">Nail &amp; thread</span>
  <h1>String Art Studio</h1>
</div>

<div class="layout">
  <div class="stage">
    <div id="canvasOuter">
      <div id="canvasWrap"><canvas id="artCanvas" width="600" height="600"></canvas></div>
      <div id="dropHint">Choose a photo to begin &darr;</div>
    </div>
  </div>

  <div class="sidebar">
    <p class="section-title">1 &middot; Design</p>

    <label class="fileBtn" for="fileInput" id="fileBtnLabel">Choose a photo&hellip;</label>
    <input type="file" id="fileInput" accept="image/*">

    <div class="switch-row" style="margin-top:16px">
      <span>Dark background</span>
      <label class="switch"><input type="checkbox" id="darkMode"><span class="slider"></span></label>
    </div>

    <div class="field">
      <label for="numPins">Nails <span class="val" id="numPinsVal">200</span></label>
      <input type="range" id="numPins" min="24" max="720" step="1" value="200">
      <div class="stat-line">Shared with the base template and the machine.</div>
    </div>
    <div class="field">
      <label for="numChords">Chords <span class="val" id="numChordsVal">3000</span></label>
      <input type="range" id="numChords" min="200" max="8000" step="50" value="3000">
    </div>
    <div class="field">
      <label for="lineWeight">Line weight <span class="val" id="lineWeightVal">18</span></label>
      <input type="range" id="lineWeight" min="4" max="60" step="1" value="18">
    </div>

    <button class="primary" id="generateBtn" disabled>Generate string art</button>
    <div class="progress-track" id="progressTrack" hidden>
      <div class="progress-fill" id="progressFill"></div>
    </div>
    <div class="progress-label" id="progressLabel" hidden>&nbsp;</div>
    <div class="stat-line" id="statLine" hidden></div>

    <div class="btn-pair" style="margin-top:10px">
      <button class="secondary" id="downloadStepsBtn" disabled>Download steps</button>
      <button class="secondary" id="downloadSvgBtn" disabled>Download SVG</button>
    </div>

    <details>
      <summary>Greeting card (A4 PDF)</summary>
      <p class="idx-sub" style="text-align:left;margin-top:8px">
        Your photo and the string art side by side on one A4 landscape sheet,
        folded down the middle. Print at 100% scale.
      </p>
      <div class="field">
        <label>Title <span class="val">under the art</span></label>
        <input id="cardTitle" type="text" maxlength="40" placeholder="optional">
      </div>
      <div class="field">
        <label>Message <span class="val">inside</span></label>
        <textarea id="cardMsg" rows="3" maxlength="240" placeholder="optional"></textarea>
      </div>
      <div class="switch-row">
        <span>Print the fold line</span>
        <label class="switch"><input type="checkbox" id="cardFold" checked><span class="slider"></span></label>
      </div>
      <button class="primary" id="cardPdfBtn" disabled style="margin-bottom:0">Download card PDF</button>
      <div class="stat-line" id="cardMsgLine">&mdash;</div>
    </details>
    <button class="primary" id="sendBtn" disabled>Send to this machine</button>
    <div class="msg" id="sendMsg"></div>

    <hr>

    <p class="section-title">2 &middot; Wrap</p>
    <div class="nail-readout" id="idxNailNum">&mdash;</div>
    <div class="idx-sub" id="idxProgressText">step -- / --</div>
    <progress id="idxBar" value="0" max="100"></progress>
    <div class="eta-grid" id="idxEta" hidden>
      <div><span class="eta-k">elapsed</span><span class="eta-v" id="etaElapsed">--</span></div>
      <div><span class="eta-k">remaining</span><span class="eta-v" id="etaLeft">--</span></div>
      <div><span class="eta-k">finishes</span><span class="eta-v" id="etaClock">--</span></div>
      <div><span class="eta-k">per nail</span><span class="eta-v" id="etaPer">--</span></div>
    </div>

    <div class="btn-pair">
      <button class="secondary" id="idxPrevBtn">&larr; Prev</button>
      <button class="primary" id="idxNextBtn" style="margin-bottom:0">Next &rarr;</button>
    </div>
    <div class="btn-pair" style="margin-top:8px">
      <button class="secondary" id="idxAutoBtn">Start Auto</button>
      <button class="danger" id="idxHomeBtn">Set Home</button>
    </div>
    <button class="secondary" id="idxFindHomeBtn" style="margin-top:8px">Find Home (limit switch)</button>
    <div class="idx-sub" id="idxSwitchText">limit switch: &mdash;</div>

    <div class="resume-banner" id="idxResumeBanner" hidden></div>

    <div class="field" style="margin-top:14px">
      <label>Go to step # <span class="val">moves progress</span></label>
      <div class="btn-pair">
        <input id="idxGotoStepVal" type="number" min="0">
        <button class="secondary" id="idxGotoStepBtn" style="width:auto;flex:none;padding:8px 16px">Go</button>
      </div>
      <div class="stat-line" id="idxGotoStepHint">&mdash;</div>
    </div>

    <div class="field">
      <label>Go to nail # <span class="val">disc only</span></label>
      <div class="btn-pair">
        <input id="idxGotoVal" type="number" min="0">
        <button class="ghost" id="idxGotoBtn" style="width:auto;flex:none;padding:8px 16px">Go</button>
      </div>
      <div class="stat-line">Rotates the disc without changing where you are in the sequence.</div>
    </div>

    <details>
      <summary>Calibrate the nail position</summary>
      <p class="idx-sub" style="text-align:left;margin-top:8px">
        Use this when the nail arriving at the feeder is not the one the machine
        names. Jog until the right nail lines up, then tell it which one that is.
        Your place in the sequence is not affected.
      </p>

      <div class="field">
        <label>Jog the disc <span class="val">nothing else changes</span></label>
        <div class="jog-row">
          <button class="ghost" data-jog="-10">&minus;10</button>
          <button class="ghost" data-jog="-3">&minus;3</button>
          <button class="ghost" data-jog="-1">&minus;1</button>
          <button class="ghost" data-jog="1">+1</button>
          <button class="ghost" data-jog="3">+3</button>
          <button class="ghost" data-jog="10">+10</button>
        </div>
        <div class="stat-line" id="calJogUnit">&mdash;</div>
      </div>

      <div class="field">
        <label>The nail at the feeder is actually #</label>
        <div class="btn-pair">
          <input id="calNailVal" type="number" min="0">
          <button class="secondary" id="calSetBtn" style="width:auto;flex:none;padding:8px 16px">Set</button>
        </div>
        <div class="stat-line" id="calOffsetText">&mdash;</div>
      </div>

      <hr>

      <p class="idx-sub" style="text-align:left">
        If it creeps a little further out every revolution, the steps-per-turn
        figure is wrong rather than steps being lost. This measures it.
      </p>
      <div class="field">
        <label>Spin this many full turns</label>
        <div class="btn-pair">
          <input id="calRevsVal" type="number" min="1" max="50" value="10">
          <button class="ghost" id="calMoveBtn" style="width:auto;flex:none;padding:8px 16px">Spin</button>
        </div>
      </div>
      <div class="field">
        <label>It finished this many nails past the start <span class="val">negative = short</span></label>
        <div class="btn-pair">
          <input id="calErrVal" type="number" value="0">
          <button class="secondary" id="calReportBtn" style="width:auto;flex:none;padding:8px 16px">Correct</button>
        </div>
      </div>
      <div class="sync-box" id="calStepsText">&mdash;</div>

      <hr>

      <div class="field">
        <label>Re-home every N nails <span class="val">0 = off</span></label>
        <input id="calRehome" type="number" min="0" max="2000" step="10">
      </div>
      <div class="stat-line" id="calRehomeText">&mdash;</div>
    </details>

    <hr>

    <p class="section-title">3 &middot; Feed</p>
    <div class="switch-row">
      <span>Auto-feed each step</span>
      <label class="switch"><input type="checkbox" id="feederAutoFeed"><span class="slider"></span></label>
    </div>
    <button class="primary" id="feederFeedBtn">Feed now</button>
    <div class="stat-line" id="feederCycleText">&mdash;</div>

    <hr>

    <p class="section-title">4 &middot; Base template</p>
    <p class="idx-sub" style="text-align:left;margin-bottom:12px">
      Print this once, glue it to the board, drill a nail at every red dot.
      The numbering matches what the generator and the indexer expect.
    </p>

    <div class="field">
      <label>Nails <span class="val" id="baseCountSaved">&mdash;</span></label>
      <input id="baseCount" type="number" min="24" max="720" step="1" value="200">
    </div>
    <div class="field">
      <label>Ring radius <span class="val">mm</span></label>
      <input id="baseRadius" type="number" min="10" step="0.5" value="200">
    </div>

    <div id="baseSync" class="sync-box">&mdash;</div>
    <div class="btn-pair" id="baseSuggestRow"></div>

    <div id="basePreview"></div>

    <button class="ghost" id="baseOpenBtn">Open full size</button>
    <button class="primary" id="baseSaveBtn" style="margin-bottom:0">Download SVG template</button>

    <details>
      <summary>Template options</summary>
      <div class="field">
        <label>Label font size <span class="val">px</span></label>
        <input id="baseFont" type="number" min="1" step="0.5" value="10">
      </div>
      <div class="field">
        <label>Outer margin <span class="val">mm</span></label>
        <input id="baseMargin" type="number" min="0" step="0.5" value="10">
      </div>
      <div class="field">
        <label>Cut margin <span class="val">mm</span></label>
        <input id="baseCutMargin" type="number" min="0" step="0.5" value="15">
      </div>
      <div class="switch-row">
        <span>Nail dots</span>
        <label class="switch"><input type="checkbox" id="baseDots" checked><span class="slider"></span></label>
      </div>
      <div class="switch-row">
        <span>Centre mark</span>
        <label class="switch"><input type="checkbox" id="baseCentre" checked><span class="slider"></span></label>
      </div>
      <div class="switch-row">
        <span>Laser-cut circle</span>
        <label class="switch"><input type="checkbox" id="baseCut"><span class="slider"></span></label>
      </div>
      <div class="stat-line" id="basePaper">&mdash;</div>
    </details>

    <hr>

    <p class="section-title">Advanced settings</p>
    <p class="idx-sub" style="text-align:left;margin-bottom:12px">
      Everything the machine remembers between reboots. Edits are held until
      you press Save, so a field you are typing in will not be overwritten by
      the status poll.
    </p>

    <details open>
      <summary>Motor</summary>
      <div class="field">
        <label>Nails <span class="val">shared</span></label>
        <input id="idxNumNails" type="number" min="24" max="720">
      </div>
      <div class="field">
        <label>Step delay <span class="val">ms / half-step</span></label>
        <input id="idxStepDelay" type="number" min="1" max="50">
      </div>
      <div class="switch-row">
        <span>Reverse rotation direction</span>
        <label class="switch"><input type="checkbox" id="idxReverseDir"><span class="slider"></span></label>
      </div>
      <div class="idx-sub" style="margin-top:0;text-align:left">
        Leave on unless the test says otherwise. Re-home after changing it.
      </div>
      <button class="ghost" id="idxDirTestBtn">Run direction test</button>
      <div class="idx-sub" id="idxDirTestText" style="text-align:left">&nbsp;</div>
      <div class="switch-row">
        <span>Find home on power-up</span>
        <label class="switch"><input type="checkbox" id="idxAutoHome"><span class="slider"></span></label>
      </div>
      <div class="idx-sub" style="margin-top:0;text-align:left">
        After a power cut, home against the limit switch and drive back to the
        saved nail. Needs the switch fitted. The disc does not start running
        again on its own.
      </div>
    </details>

    <details open>
      <summary>Wrap cycle</summary>
      <div class="switch-row">
        <span>Wrap thread around each nail</span>
        <label class="switch"><input type="checkbox" id="wrapMode"><span class="slider"></span></label>
      </div>
      <div class="idx-sub" style="margin-top:0;text-align:left">
        Off = the old behaviour: the servo just pays thread out and the disc
        never moves while it is extended. That cannot hook a nail.
      </div>
      <div class="field">
        <label>Approach offset <span class="val" id="wrapStepsNails">&mdash;</span></label>
        <input id="wrapSteps" type="number" min="1" max="200">
      </div>
      <div class="field">
        <label>Sweep distance <span class="val" id="wrapSweepNails">&mdash;</span></label>
        <input id="wrapSweep" type="number" min="1" max="400">
      </div>
      <button class="ghost" id="wrapRecalcBtn">Reset to the values for this nail count</button>
      <div class="field">
        <label>Wait after tube moves in <span class="val">ms</span></label>
        <input id="wrapHoldInMs" type="number" min="0" max="10000" step="50">
      </div>
      <div class="field">
        <label>Wait after sweep <span class="val">ms</span></label>
        <input id="wrapHoldSweepMs" type="number" min="0" max="10000" step="50">
      </div>
      <div class="field">
        <label>Servo speed <span class="val">&deg; per step</span></label>
        <input id="servoSlewDeg" type="number" min="1" max="90">
      </div>
      <div class="field">
        <label>Servo step interval <span class="val">ms</span></label>
        <input id="servoSlewMs" type="number" min="0" max="500" step="5">
      </div>
      <div class="switch-row">
        <span>Approach from the other side</span>
        <label class="switch"><input type="checkbox" id="wrapDirFlip"><span class="slider"></span></label>
      </div>
      <div class="sync-box" id="wrapBreakdown">&mdash;</div>
      <div class="btn-pair">
        <button class="ghost" id="wrapTestCwBtn">Test wrap, coming from &minus;</button>
        <button class="ghost" id="wrapTestCcwBtn">Test wrap, coming from +</button>
      </div>
      <button class="ghost" id="wrapTestBtn">Test one wrap, as the sequence would</button>
      <div class="stat-line" id="wrapTestText">&nbsp;</div>
    </details>

    <details open>
      <summary>Timings</summary>
      <div class="field">
        <label>Settle before feed <span class="val">ms</span></label>
        <input id="feederSettleMs" type="number" min="0" max="10000" step="50">
      </div>
      <div class="field">
        <label>Servo travel <span class="val">ms</span></label>
        <input id="feederPulseMs" type="number" min="0" max="10000" step="50">
      </div>
      <div class="field">
        <label>Recover after feed <span class="val">ms</span></label>
        <input id="feederRecoverMs" type="number" min="0" max="10000" step="50">
      </div>
      <div class="field">
        <label>Dwell before next nail <span class="val">ms</span></label>
        <input id="idxAutoMs" type="number" min="0" max="120000" step="100">
      </div>
      <div class="sync-box" id="cycleBreakdown">&mdash;</div>
    </details>

    <details>
      <summary>Feeder servo (SG90) angles</summary>
      <p class="idx-sub" style="text-align:left;margin-top:8px">
        The arm follows these sliders as you drag them, so set them by watching
        the tube rather than by guessing numbers. Rest is where it sits clear of
        the nails; Feed is where the tube is out past the ring.
      </p>

      <div class="field">
        <label>Rest angle <span class="val" id="restVal">&mdash;</span></label>
        <input id="feederRestAngle" type="range" min="0" max="180" step="1">
      </div>
      <div class="field">
        <label>Feed angle <span class="val" id="feedVal">&mdash;</span></label>
        <input id="feederFeedAngle" type="range" min="0" max="180" step="1">
      </div>

      <div class="btn-pair">
        <button class="ghost" id="servoGoRest">Hold at rest</button>
        <button class="ghost" id="servoGoFeed">Hold at feed</button>
      </div>
      <button class="ghost" id="servoSwing">Swing rest &rarr; feed &rarr; rest</button>
      <div class="stat-line" id="servoLiveText">&mdash;</div>
    </details>

    <div class="msg" id="advMsg"></div>
    <button class="primary" id="advSaveBtn">Save advanced settings</button>
    <button class="ghost" id="advResetBtn">Restore defaults</button>
  </div>
</div>

<div id="cropOverlay" hidden>
  <div id="cropBox">
    <h3>Frame the artwork</h3>
    <div id="cropImageWrap"><img id="cropImage" alt="Photo to crop"></div>
    <div class="crop-actions">
      <button class="ghost" id="cropCancelBtn">Cancel</button>
      <button class="primary" id="cropConfirmBtn" style="margin-bottom:0">Use this crop</button>
    </div>
  </div>
</div>

<script>
/* ---- Shared nail count -------------------------------------------------
   One number, three views: the Design slider, the Base template input, and
   the machine settings field. Editing any of them updates the others
   immediately and saves to the machine once typing stops, so the printed
   base, the generated sequence and the indexer can never disagree.       */
window.NailCount = (function(){
  "use strict";
  const MIN = 24, MAX = 720, SAVE_DELAY_MS = 600;
  let value = 200;
  let unsaved = false;      // a local edit not yet written to the machine
  let saveTimer = null;
  const subs = [];

  const clamp = v => Math.min(MAX, Math.max(MIN, Math.round(+v) || MIN));
  const notify = src => subs.forEach(fn => { try { fn(value, src); } catch (e) {} });

  async function save(){
    const wanted = value;
    try {
      await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "numNails=" + wanted,
      });
    } catch (e) { /* offline: the views stay in sync, the machine catches up later */ }
    // Cleared either way. Leaving it set would block the status poll from ever
    // correcting the page again.
    if (value === wanted) { unsaved = false; notify("saved"); }
  }

  return {
    get: () => value,
    isUnsaved: () => unsaved,
    // An edit made in the browser, from whichever control.
    set(v, src){
      v = clamp(v);
      if (v === value) return;
      value = v;
      unsaved = true;
      notify(src || "local");
      clearTimeout(saveTimer);
      saveTimer = setTimeout(save, SAVE_DELAY_MS);
    },
    // A value read back from /status. Ignored while an edit is still pending,
    // so a poll landing mid-keystroke cannot yank the field back.
    adopt(v){
      if (unsaved) return;
      v = clamp(v);
      if (v === value) return;
      value = v;
      notify("machine");
    },
    on(fn){ subs.push(fn); fn(value, "init"); },
  };
})();
</script>
<script>
/* ---- vendored cropper.min.js ---- */
/*!
 * Cropper.js v1.6.1
 * https://fengyuanchen.github.io/cropperjs
 *
 * Copyright 2015-present Chen Fengyuan
 * Released under the MIT license
 *
 * Date: 2023-09-17T03:44:19.860Z
 */
!function(t,e){"object"==typeof exports&&"undefined"!=typeof module?module.exports=e():"function"==typeof define&&define.amd?define(e):(t="undefined"!=typeof globalThis?globalThis:t||self).Cropper=e()}(this,function(){"use strict";function C(e,t){var i,a=Object.keys(e);return Object.getOwnPropertySymbols&&(i=Object.getOwnPropertySymbols(e),t&&(i=i.filter(function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable})),a.push.apply(a,i)),a}function S(a){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?C(Object(n),!0).forEach(function(t){var e,i;e=a,i=n[t=t],(t=P(t))in e?Object.defineProperty(e,t,{value:i,enumerable:!0,configurable:!0,writable:!0}):e[t]=i}):Object.getOwnPropertyDescriptors?Object.defineProperties(a,Object.getOwnPropertyDescriptors(n)):C(Object(n)).forEach(function(t){Object.defineProperty(a,t,Object.getOwnPropertyDescriptor(n,t))})}return a}function D(t){return(D="function"==typeof Symbol&&"symbol"==typeof Symbol.iterator?function(t){return typeof t}:function(t){return t&&"function"==typeof Symbol&&t.constructor===Symbol&&t!==Symbol.prototype?"symbol":typeof t})(t)}function j(t,e){for(var i=0;i<e.length;i++){var a=e[i];a.enumerable=a.enumerable||!1,a.configurable=!0,"value"in a&&(a.writable=!0),Object.defineProperty(t,P(a.key),a)}}function A(t){return function(t){if(Array.isArray(t))return a(t)}(t)||function(t){if("undefined"!=typeof Symbol&&null!=t[Symbol.iterator]||null!=t["@@iterator"])return Array.from(t)}(t)||function(t,e){var i;if(t)return"string"==typeof t?a(t,e):"Map"===(i="Object"===(i=Object.prototype.toString.call(t).slice(8,-1))&&t.constructor?t.constructor.name:i)||"Set"===i?Array.from(t):"Arguments"===i||/^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(i)?a(t,e):void 0}(t)||function(){throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.")}()}function a(t,e){(null==e||e>t.length)&&(e=t.length);for(var i=0,a=new Array(e);i<e;i++)a[i]=t[i];return a}function P(t){t=function(t,e){if("object"!=typeof t||null===t)return t;var i=t[Symbol.toPrimitive];if(void 0===i)return("string"===e?String:Number)(t);if("object"!=typeof(i=i.call(t,e||"default")))return i;throw new TypeError("@@toPrimitive must return a primitive value.")}(t,"string");return"symbol"==typeof t?t:String(t)}var t="undefined"!=typeof window&&void 0!==window.document,h=t?window:{},e=!(!t||!h.document.documentElement)&&"ontouchstart"in h.document.documentElement,i=t&&"PointerEvent"in h,c="cropper",I="all",U="crop",q="move",$="zoom",B="e",k="w",O="s",T="n",E="ne",W="nw",H="se",N="sw",Q="".concat(c,"-crop"),K="".concat(c,"-disabled"),L="".concat(c,"-hidden"),Z="".concat(c,"-hide"),G="".concat(c,"-invisible"),n="".concat(c,"-modal"),V="".concat(c,"-move"),d="".concat(c,"Action"),m="".concat(c,"Preview"),F="crop",J="move",_="none",tt="crop",et="cropend",it="cropmove",at="cropstart",nt="dblclick",ot=i?"pointerdown":e?"touchstart":"mousedown",ht=i?"pointermove":e?"touchmove":"mousemove",rt=i?"pointerup pointercancel":e?"touchend touchcancel":"mouseup",st="zoom",ct="image/jpeg",dt=/^e|w|s|n|se|sw|ne|nw|all|crop|move|zoom$/,lt=/^data:/,pt=/^data:image\/jpeg;base64,/,mt=/^img|canvas$/i,ut={viewMode:0,dragMode:F,initialAspectRatio:NaN,aspectRatio:NaN,data:null,preview:"",responsive:!0,restore:!0,checkCrossOrigin:!0,checkOrientation:!0,modal:!0,guides:!0,center:!0,highlight:!0,background:!0,autoCrop:!0,autoCropArea:.8,movable:!0,rotatable:!0,scalable:!0,zoomable:!0,zoomOnTouch:!0,zoomOnWheel:!0,wheelZoomRatio:.1,cropBoxMovable:!0,cropBoxResizable:!0,toggleDragModeOnDblclick:!0,minCanvasWidth:0,minCanvasHeight:0,minCropBoxWidth:0,minCropBoxHeight:0,minContainerWidth:200,minContainerHeight:100,ready:null,cropstart:null,cropmove:null,cropend:null,crop:null,zoom:null},gt=Number.isNaN||h.isNaN;function p(t){return"number"==typeof t&&!gt(t)}function ft(t){return 0<t&&t<1/0}function vt(t){return void 0===t}function o(t){return"object"===D(t)&&null!==t}var wt=Object.prototype.hasOwnProperty;function u(t){if(!o(t))return!1;try{var e=t.constructor,i=e.prototype;return e&&i&&wt.call(i,"isPrototypeOf")}catch(t){return!1}}function l(t){return"function"==typeof t}var bt=Array.prototype.slice;function yt(t){return Array.from?Array.from(t):bt.call(t)}function z(i,a){return i&&l(a)&&(Array.isArray(i)||p(i.length)?yt(i).forEach(function(t,e){a.call(i,t,e,i)}):o(i)&&Object.keys(i).forEach(function(t){a.call(i,i[t],t,i)})),i}var g=Object.assign||function(i){for(var t=arguments.length,e=new Array(1<t?t-1:0),a=1;a<t;a++)e[a-1]=arguments[a];return o(i)&&0<e.length&&e.forEach(function(e){o(e)&&Object.keys(e).forEach(function(t){i[t]=e[t]})}),i},xt=/\.\d*(?:0|9){12}\d*$/;function Y(t,e){e=1<arguments.length&&void 0!==e?e:1e11;return xt.test(t)?Math.round(t*e)/e:t}var Mt=/^width|height|left|top|marginLeft|marginTop$/;function f(t,e){var i=t.style;z(e,function(t,e){Mt.test(e)&&p(t)&&(t="".concat(t,"px")),i[e]=t})}function v(t,e){var i;e&&(p(t.length)?z(t,function(t){v(t,e)}):t.classList?t.classList.add(e):(i=t.className.trim())?i.indexOf(e)<0&&(t.className="".concat(i," ").concat(e)):t.className=e)}function X(t,e){e&&(p(t.length)?z(t,function(t){X(t,e)}):t.classList?t.classList.remove(e):0<=t.className.indexOf(e)&&(t.className=t.className.replace(e,"")))}function r(t,e,i){e&&(p(t.length)?z(t,function(t){r(t,e,i)}):(i?v:X)(t,e))}var Ct=/([a-z\d])([A-Z])/g;function Dt(t){return t.replace(Ct,"$1-$2").toLowerCase()}function Bt(t,e){return o(t[e])?t[e]:t.dataset?t.dataset[e]:t.getAttribute("data-".concat(Dt(e)))}function w(t,e,i){o(i)?t[e]=i:t.dataset?t.dataset[e]=i:t.setAttribute("data-".concat(Dt(e)),i)}var kt,Ot,Tt=/\s\s*/,Et=(Ot=!1,t&&(kt=!1,i=function(){},e=Object.defineProperty({},"once",{get:function(){return Ot=!0,kt},set:function(t){kt=t}}),h.addEventListener("test",i,e),h.removeEventListener("test",i,e)),Ot);function s(i,t,a,e){var n=3<arguments.length&&void 0!==e?e:{},o=a;t.trim().split(Tt).forEach(function(t){var e;Et||(e=i.listeners)&&e[t]&&e[t][a]&&(o=e[t][a],delete e[t][a],0===Object.keys(e[t]).length&&delete e[t],0===Object.keys(e).length)&&delete i.listeners,i.removeEventListener(t,o,n)})}function b(o,t,h,e){var r=3<arguments.length&&void 0!==e?e:{},s=h;t.trim().split(Tt).forEach(function(a){var t,n;r.once&&!Et&&(t=o.listeners,s=function(){delete n[a][h],o.removeEventListener(a,s,r);for(var t=arguments.length,e=new Array(t),i=0;i<t;i++)e[i]=arguments[i];h.apply(o,e)},(n=void 0===t?{}:t)[a]||(n[a]={}),n[a][h]&&o.removeEventListener(a,n[a][h],r),n[a][h]=s,o.listeners=n),o.addEventListener(a,s,r)})}function y(t,e,i){var a;return l(Event)&&l(CustomEvent)?a=new CustomEvent(e,{detail:i,bubbles:!0,cancelable:!0}):(a=document.createEvent("CustomEvent")).initCustomEvent(e,!0,!0,i),t.dispatchEvent(a)}function Wt(t){t=t.getBoundingClientRect();return{left:t.left+(window.pageXOffset-document.documentElement.clientLeft),top:t.top+(window.pageYOffset-document.documentElement.clientTop)}}var Ht=h.location,Nt=/^(\w+:)\/\/([^:/?#]*):?(\d*)/i;function Lt(t){t=t.match(Nt);return null!==t&&(t[1]!==Ht.protocol||t[2]!==Ht.hostname||t[3]!==Ht.port)}function zt(t){var e="timestamp=".concat((new Date).getTime());return t+(-1===t.indexOf("?")?"?":"&")+e}function x(t){var e=t.rotate,i=t.scaleX,a=t.scaleY,n=t.translateX,t=t.translateY,o=[],n=(p(n)&&0!==n&&o.push("translateX(".concat(n,"px)")),p(t)&&0!==t&&o.push("translateY(".concat(t,"px)")),p(e)&&0!==e&&o.push("rotate(".concat(e,"deg)")),p(i)&&1!==i&&o.push("scaleX(".concat(i,")")),p(a)&&1!==a&&o.push("scaleY(".concat(a,")")),o.length?o.join(" "):"none");return{WebkitTransform:n,msTransform:n,transform:n}}function M(t,e){var i=t.pageX,t=t.pageY,a={endX:i,endY:t};return e?a:S({startX:i,startY:t},a)}function R(t,e){var i,a=t.aspectRatio,n=t.height,t=t.width,e=1<arguments.length&&void 0!==e?e:"contain",o=ft(t),h=ft(n);return o&&h?(i=n*a,"contain"===e&&t<i||"cover"===e&&i<t?n=t/a:t=n*a):o?n=t/a:h&&(t=n*a),{width:t,height:n}}var Yt=String.fromCharCode;var Xt=/^data:.*,/;function Rt(t){var e,i,a,n,o,h,r,s=new DataView(t);try{if(255===s.getUint8(0)&&216===s.getUint8(1))for(var c=s.byteLength,d=2;d+1<c;){if(255===s.getUint8(d)&&225===s.getUint8(d+1)){i=d;break}d+=1}if(a=i&&(n=i+10,"Exif"===function(t,e,i){var a="";i+=e;for(var n=e;n<i;n+=1)a+=Yt(t.getUint8(n));return a}(s,i+4,4))&&((r=18761===(o=s.getUint16(n)))||19789===o)&&42===s.getUint16(n+2,r)&&8<=(h=s.getUint32(n+4,r))?n+h:a)for(var l,p=s.getUint16(a,r),m=0;m<p;m+=1)if(l=a+12*m+2,274===s.getUint16(l,r)){l+=8,e=s.getUint16(l,r),s.setUint16(l,1,r);break}}catch(t){e=1}return e}var t={render:function(){this.initContainer(),this.initCanvas(),this.initCropBox(),this.renderCanvas(),this.cropped&&this.renderCropBox()},initContainer:function(){var t=this.element,e=this.options,i=this.container,a=this.cropper,n=Number(e.minContainerWidth),e=Number(e.minContainerHeight),n=(v(a,L),X(t,L),{width:Math.max(i.offsetWidth,0<=n?n:200),height:Math.max(i.offsetHeight,0<=e?e:100)});f(a,{width:(this.containerData=n).width,height:n.height}),v(t,L),X(a,L)},initCanvas:function(){var t=this.containerData,e=this.imageData,i=this.options.viewMode,a=Math.abs(e.rotate)%180==90,n=a?e.naturalHeight:e.naturalWidth,a=a?e.naturalWidth:e.naturalHeight,e=n/a,o=t.width,h=t.height,e=(t.height*e>t.width?3===i?o=t.height*e:h=t.width/e:3===i?h=t.width/e:o=t.height*e,{aspectRatio:e,naturalWidth:n,naturalHeight:a,width:o,height:h});this.canvasData=e,this.limited=1===i||2===i,this.limitCanvas(!0,!0),e.width=Math.min(Math.max(e.width,e.minWidth),e.maxWidth),e.height=Math.min(Math.max(e.height,e.minHeight),e.maxHeight),e.left=(t.width-e.width)/2,e.top=(t.height-e.height)/2,e.oldLeft=e.left,e.oldTop=e.top,this.initialCanvasData=g({},e)},limitCanvas:function(t,e){var i=this.options,a=this.containerData,n=this.canvasData,o=this.cropBoxData,h=i.viewMode,r=n.aspectRatio,s=this.cropped&&o;t&&(t=Number(i.minCanvasWidth)||0,i=Number(i.minCanvasHeight)||0,1<h?(t=Math.max(t,a.width),i=Math.max(i,a.height),3===h&&(t<i*r?t=i*r:i=t/r)):0<h&&(t?t=Math.max(t,s?o.width:0):i?i=Math.max(i,s?o.height:0):s&&((t=o.width)<(i=o.height)*r?t=i*r:i=t/r)),t=(r=R({aspectRatio:r,width:t,height:i})).width,i=r.height,n.minWidth=t,n.minHeight=i,n.maxWidth=1/0,n.maxHeight=1/0),e&&((s?0:1)<h?(r=a.width-n.width,t=a.height-n.height,n.minLeft=Math.min(0,r),n.minTop=Math.min(0,t),n.maxLeft=Math.max(0,r),n.maxTop=Math.max(0,t),s&&this.limited&&(n.minLeft=Math.min(o.left,o.left+(o.width-n.width)),n.minTop=Math.min(o.top,o.top+(o.height-n.height)),n.maxLeft=o.left,n.maxTop=o.top,2===h)&&(n.width>=a.width&&(n.minLeft=Math.min(0,r),n.maxLeft=Math.max(0,r)),n.height>=a.height)&&(n.minTop=Math.min(0,t),n.maxTop=Math.max(0,t))):(n.minLeft=-n.width,n.minTop=-n.height,n.maxLeft=a.width,n.maxTop=a.height))},renderCanvas:function(t,e){var i,a,n,o,h=this.canvasData,r=this.imageData;e&&(e={width:r.naturalWidth*Math.abs(r.scaleX||1),height:r.naturalHeight*Math.abs(r.scaleY||1),degree:r.rotate||0},r=e.width,o=e.height,e=e.degree,i=90==(e=Math.abs(e)%180)?{width:o,height:r}:(a=e%90*Math.PI/180,i=Math.sin(a),n=r*(a=Math.cos(a))+o*i,r=r*i+o*a,90<e?{width:r,height:n}:{width:n,height:r}),a=h.width*((o=i.width)/h.naturalWidth),n=h.height*((e=i.height)/h.naturalHeight),h.left-=(a-h.width)/2,h.top-=(n-h.height)/2,h.width=a,h.height=n,h.aspectRatio=o/e,h.naturalWidth=o,h.naturalHeight=e,this.limitCanvas(!0,!1)),(h.width>h.maxWidth||h.width<h.minWidth)&&(h.left=h.oldLeft),(h.height>h.maxHeight||h.height<h.minHeight)&&(h.top=h.oldTop),h.width=Math.min(Math.max(h.width,h.minWidth),h.maxWidth),h.height=Math.min(Math.max(h.height,h.minHeight),h.maxHeight),this.limitCanvas(!1,!0),h.left=Math.min(Math.max(h.left,h.minLeft),h.maxLeft),h.top=Math.min(Math.max(h.top,h.minTop),h.maxTop),h.oldLeft=h.left,h.oldTop=h.top,f(this.canvas,g({width:h.width,height:h.height},x({translateX:h.left,translateY:h.top}))),this.renderImage(t),this.cropped&&this.limited&&this.limitCropBox(!0,!0)},renderImage:function(t){var e=this.canvasData,i=this.imageData,a=i.naturalWidth*(e.width/e.naturalWidth),n=i.naturalHeight*(e.height/e.naturalHeight);g(i,{width:a,height:n,left:(e.width-a)/2,top:(e.height-n)/2}),f(this.image,g({width:i.width,height:i.height},x(g({translateX:i.left,translateY:i.top},i)))),t&&this.output()},initCropBox:function(){var t=this.options,e=this.canvasData,i=t.aspectRatio||t.initialAspectRatio,t=Number(t.autoCropArea)||.8,a={width:e.width,height:e.height};i&&(e.height*i>e.width?a.height=a.width/i:a.width=a.height*i),this.cropBoxData=a,this.limitCropBox(!0,!0),a.width=Math.min(Math.max(a.width,a.minWidth),a.maxWidth),a.height=Math.min(Math.max(a.height,a.minHeight),a.maxHeight),a.width=Math.max(a.minWidth,a.width*t),a.height=Math.max(a.minHeight,a.height*t),a.left=e.left+(e.width-a.width)/2,a.top=e.top+(e.height-a.height)/2,a.oldLeft=a.left,a.oldTop=a.top,this.initialCropBoxData=g({},a)},limitCropBox:function(t,e){var i,a,n=this.options,o=this.containerData,h=this.canvasData,r=this.cropBoxData,s=this.limited,c=n.aspectRatio;t&&(t=Number(n.minCropBoxWidth)||0,n=Number(n.minCropBoxHeight)||0,i=s?Math.min(o.width,h.width,h.width+h.left,o.width-h.left):o.width,a=s?Math.min(o.height,h.height,h.height+h.top,o.height-h.top):o.height,t=Math.min(t,o.width),n=Math.min(n,o.height),c&&(t&&n?t<n*c?n=t/c:t=n*c:t?n=t/c:n&&(t=n*c),i<a*c?a=i/c:i=a*c),r.minWidth=Math.min(t,i),r.minHeight=Math.min(n,a),r.maxWidth=i,r.maxHeight=a),e&&(s?(r.minLeft=Math.max(0,h.left),r.minTop=Math.max(0,h.top),r.maxLeft=Math.min(o.width,h.left+h.width)-r.width,r.maxTop=Math.min(o.height,h.top+h.height)-r.height):(r.minLeft=0,r.minTop=0,r.maxLeft=o.width-r.width,r.maxTop=o.height-r.height))},renderCropBox:function(){var t=this.options,e=this.containerData,i=this.cropBoxData;(i.width>i.maxWidth||i.width<i.minWidth)&&(i.left=i.oldLeft),(i.height>i.maxHeight||i.height<i.minHeight)&&(i.top=i.oldTop),i.width=Math.min(Math.max(i.width,i.minWidth),i.maxWidth),i.height=Math.min(Math.max(i.height,i.minHeight),i.maxHeight),this.limitCropBox(!1,!0),i.left=Math.min(Math.max(i.left,i.minLeft),i.maxLeft),i.top=Math.min(Math.max(i.top,i.minTop),i.maxTop),i.oldLeft=i.left,i.oldTop=i.top,t.movable&&t.cropBoxMovable&&w(this.face,d,i.width>=e.width&&i.height>=e.height?q:I),f(this.cropBox,g({width:i.width,height:i.height},x({translateX:i.left,translateY:i.top}))),this.cropped&&this.limited&&this.limitCanvas(!0,!0),this.disabled||this.output()},output:function(){this.preview(),y(this.element,tt,this.getData())}},i={initPreview:function(){var t=this.element,i=this.crossOrigin,e=this.options.preview,a=i?this.crossOriginUrl:this.url,n=t.alt||"The image to preview",o=document.createElement("img");i&&(o.crossOrigin=i),o.src=a,o.alt=n,this.viewBox.appendChild(o),this.viewBoxImage=o,e&&("string"==typeof(o=e)?o=t.ownerDocument.querySelectorAll(e):e.querySelector&&(o=[e]),z(this.previews=o,function(t){var e=document.createElement("img");w(t,m,{width:t.offsetWidth,height:t.offsetHeight,html:t.innerHTML}),i&&(e.crossOrigin=i),e.src=a,e.alt=n,e.style.cssText='display:block;width:100%;height:auto;min-width:0!important;min-height:0!important;max-width:none!important;max-height:none!important;image-orientation:0deg!important;"',t.innerHTML="",t.appendChild(e)}))},resetPreview:function(){z(this.previews,function(e){var i=Bt(e,m),i=(f(e,{width:i.width,height:i.height}),e.innerHTML=i.html,e),e=m;if(o(i[e]))try{delete i[e]}catch(t){i[e]=void 0}else if(i.dataset)try{delete i.dataset[e]}catch(t){i.dataset[e]=void 0}else i.removeAttribute("data-".concat(Dt(e)))})},preview:function(){var h=this.imageData,t=this.canvasData,e=this.cropBoxData,r=e.width,s=e.height,c=h.width,d=h.height,l=e.left-t.left-h.left,p=e.top-t.top-h.top;this.cropped&&!this.disabled&&(f(this.viewBoxImage,g({width:c,height:d},x(g({translateX:-l,translateY:-p},h)))),z(this.previews,function(t){var e=Bt(t,m),i=e.width,e=e.height,a=i,n=e,o=1;r&&(n=s*(o=i/r)),s&&e<n&&(a=r*(o=e/s),n=e),f(t,{width:a,height:n}),f(t.getElementsByTagName("img")[0],g({width:c*o,height:d*o},x(g({translateX:-l*o,translateY:-p*o},h))))}))}},e={bind:function(){var t=this.element,e=this.options,i=this.cropper;l(e.cropstart)&&b(t,at,e.cropstart),l(e.cropmove)&&b(t,it,e.cropmove),l(e.cropend)&&b(t,et,e.cropend),l(e.crop)&&b(t,tt,e.crop),l(e.zoom)&&b(t,st,e.zoom),b(i,ot,this.onCropStart=this.cropStart.bind(this)),e.zoomable&&e.zoomOnWheel&&b(i,"wheel",this.onWheel=this.wheel.bind(this),{passive:!1,capture:!0}),e.toggleDragModeOnDblclick&&b(i,nt,this.onDblclick=this.dblclick.bind(this)),b(t.ownerDocument,ht,this.onCropMove=this.cropMove.bind(this)),b(t.ownerDocument,rt,this.onCropEnd=this.cropEnd.bind(this)),e.responsive&&b(window,"resize",this.onResize=this.resize.bind(this))},unbind:function(){var t=this.element,e=this.options,i=this.cropper;l(e.cropstart)&&s(t,at,e.cropstart),l(e.cropmove)&&s(t,it,e.cropmove),l(e.cropend)&&s(t,et,e.cropend),l(e.crop)&&s(t,tt,e.crop),l(e.zoom)&&s(t,st,e.zoom),s(i,ot,this.onCropStart),e.zoomable&&e.zoomOnWheel&&s(i,"wheel",this.onWheel,{passive:!1,capture:!0}),e.toggleDragModeOnDblclick&&s(i,nt,this.onDblclick),s(t.ownerDocument,ht,this.onCropMove),s(t.ownerDocument,rt,this.onCropEnd),e.responsive&&s(window,"resize",this.onResize)}},St={resize:function(){var t,e,i,a,n,o,h;this.disabled||(t=this.options,a=this.container,e=this.containerData,i=a.offsetWidth/e.width,a=a.offsetHeight/e.height,1!=(n=Math.abs(i-1)>Math.abs(a-1)?i:a)&&(t.restore&&(o=this.getCanvasData(),h=this.getCropBoxData()),this.render(),t.restore)&&(this.setCanvasData(z(o,function(t,e){o[e]=t*n})),this.setCropBoxData(z(h,function(t,e){h[e]=t*n}))))},dblclick:function(){var t,e;this.disabled||this.options.dragMode===_||this.setDragMode((t=this.dragBox,e=Q,(t.classList?t.classList.contains(e):-1<t.className.indexOf(e))?J:F))},wheel:function(t){var e=this,i=Number(this.options.wheelZoomRatio)||.1,a=1;this.disabled||(t.preventDefault(),this.wheeling)||(this.wheeling=!0,setTimeout(function(){e.wheeling=!1},50),t.deltaY?a=0<t.deltaY?1:-1:t.wheelDelta?a=-t.wheelDelta/120:t.detail&&(a=0<t.detail?1:-1),this.zoom(-a*i,t))},cropStart:function(t){var e,i=t.buttons,a=t.button;this.disabled||("mousedown"===t.type||"pointerdown"===t.type&&"mouse"===t.pointerType)&&(p(i)&&1!==i||p(a)&&0!==a||t.ctrlKey)||(i=this.options,e=this.pointers,t.changedTouches?z(t.changedTouches,function(t){e[t.identifier]=M(t)}):e[t.pointerId||0]=M(t),a=1<Object.keys(e).length&&i.zoomable&&i.zoomOnTouch?$:Bt(t.target,d),dt.test(a)&&!1!==y(this.element,at,{originalEvent:t,action:a})&&(t.preventDefault(),this.action=a,this.cropping=!1,a===U)&&(this.cropping=!0,v(this.dragBox,n)))},cropMove:function(t){var e,i=this.action;!this.disabled&&i&&(e=this.pointers,t.preventDefault(),!1!==y(this.element,it,{originalEvent:t,action:i}))&&(t.changedTouches?z(t.changedTouches,function(t){g(e[t.identifier]||{},M(t,!0))}):g(e[t.pointerId||0]||{},M(t,!0)),this.change(t))},cropEnd:function(t){var e,i;this.disabled||(e=this.action,i=this.pointers,t.changedTouches?z(t.changedTouches,function(t){delete i[t.identifier]}):delete i[t.pointerId||0],e&&(t.preventDefault(),Object.keys(i).length||(this.action=""),this.cropping&&(this.cropping=!1,r(this.dragBox,n,this.cropped&&this.options.modal)),y(this.element,et,{originalEvent:t,action:e})))}},jt={change:function(t){function e(t){switch(t){case B:f+D.x>y&&(D.x=y-f);break;case k:p+D.x<w&&(D.x=w-p);break;case T:m+D.y<b&&(D.y=b-m);break;case O:v+D.y>x&&(D.y=x-v)}}var i,a,o,n=this.options,h=this.canvasData,r=this.containerData,s=this.cropBoxData,c=this.pointers,d=this.action,l=n.aspectRatio,p=s.left,m=s.top,u=s.width,g=s.height,f=p+u,v=m+g,w=0,b=0,y=r.width,x=r.height,M=!0,C=(!l&&t.shiftKey&&(l=u&&g?u/g:1),this.limited&&(w=s.minLeft,b=s.minTop,y=w+Math.min(r.width,h.width,h.left+h.width),x=b+Math.min(r.height,h.height,h.top+h.height)),c[Object.keys(c)[0]]),D={x:C.endX-C.startX,y:C.endY-C.startY};switch(d){case I:p+=D.x,m+=D.y;break;case B:0<=D.x&&(y<=f||l&&(m<=b||x<=v))?M=!1:(e(B),(u+=D.x)<0&&(d=k,p-=u=-u),l&&(m+=(s.height-(g=u/l))/2));break;case T:D.y<=0&&(m<=b||l&&(p<=w||y<=f))?M=!1:(e(T),g-=D.y,m+=D.y,g<0&&(d=O,m-=g=-g),l&&(p+=(s.width-(u=g*l))/2));break;case k:D.x<=0&&(p<=w||l&&(m<=b||x<=v))?M=!1:(e(k),u-=D.x,p+=D.x,u<0&&(d=B,p-=u=-u),l&&(m+=(s.height-(g=u/l))/2));break;case O:0<=D.y&&(x<=v||l&&(p<=w||y<=f))?M=!1:(e(O),(g+=D.y)<0&&(d=T,m-=g=-g),l&&(p+=(s.width-(u=g*l))/2));break;case E:if(l){if(D.y<=0&&(m<=b||y<=f)){M=!1;break}e(T),g-=D.y,m+=D.y,u=g*l}else e(T),e(B),!(0<=D.x)||f<y?u+=D.x:D.y<=0&&m<=b&&(M=!1),(!(D.y<=0)||b<m)&&(g-=D.y,m+=D.y);u<0&&g<0?(d=N,m-=g=-g,p-=u=-u):u<0?(d=W,p-=u=-u):g<0&&(d=H,m-=g=-g);break;case W:if(l){if(D.y<=0&&(m<=b||p<=w)){M=!1;break}e(T),g-=D.y,m+=D.y,p+=s.width-(u=g*l)}else e(T),e(k),!(D.x<=0)||w<p?(u-=D.x,p+=D.x):D.y<=0&&m<=b&&(M=!1),(!(D.y<=0)||b<m)&&(g-=D.y,m+=D.y);u<0&&g<0?(d=H,m-=g=-g,p-=u=-u):u<0?(d=E,p-=u=-u):g<0&&(d=N,m-=g=-g);break;case N:if(l){if(D.x<=0&&(p<=w||x<=v)){M=!1;break}e(k),u-=D.x,p+=D.x,g=u/l}else e(O),e(k),!(D.x<=0)||w<p?(u-=D.x,p+=D.x):0<=D.y&&x<=v&&(M=!1),(!(0<=D.y)||v<x)&&(g+=D.y);u<0&&g<0?(d=E,m-=g=-g,p-=u=-u):u<0?(d=H,p-=u=-u):g<0&&(d=W,m-=g=-g);break;case H:if(l){if(0<=D.x&&(y<=f||x<=v)){M=!1;break}e(B),g=(u+=D.x)/l}else e(O),e(B),!(0<=D.x)||f<y?u+=D.x:0<=D.y&&x<=v&&(M=!1),(!(0<=D.y)||v<x)&&(g+=D.y);u<0&&g<0?(d=W,m-=g=-g,p-=u=-u):u<0?(d=N,p-=u=-u):g<0&&(d=E,m-=g=-g);break;case q:this.move(D.x,D.y),M=!1;break;case $:this.zoom((a=S({},i=c),o=0,z(i,function(n,t){delete a[t],z(a,function(t){var e=Math.abs(n.startX-t.startX),i=Math.abs(n.startY-t.startY),a=Math.abs(n.endX-t.endX),t=Math.abs(n.endY-t.endY),e=Math.sqrt(e*e+i*i),i=(Math.sqrt(a*a+t*t)-e)/e;Math.abs(i)>Math.abs(o)&&(o=i)})}),o),t),M=!1;break;case U:D.x&&D.y?(i=Wt(this.cropper),p=C.startX-i.left,m=C.startY-i.top,u=s.minWidth,g=s.minHeight,0<D.x?d=0<D.y?H:E:D.x<0&&(p-=u,d=0<D.y?N:W),D.y<0&&(m-=g),this.cropped||(X(this.cropBox,L),this.cropped=!0,this.limited&&this.limitCropBox(!0,!0))):M=!1}M&&(s.width=u,s.height=g,s.left=p,s.top=m,this.action=d,this.renderCropBox()),z(c,function(t){t.startX=t.endX,t.startY=t.endY})}},At={crop:function(){return!this.ready||this.cropped||this.disabled||(this.cropped=!0,this.limitCropBox(!0,!0),this.options.modal&&v(this.dragBox,n),X(this.cropBox,L),this.setCropBoxData(this.initialCropBoxData)),this},reset:function(){return this.ready&&!this.disabled&&(this.imageData=g({},this.initialImageData),this.canvasData=g({},this.initialCanvasData),this.cropBoxData=g({},this.initialCropBoxData),this.renderCanvas(),this.cropped)&&this.renderCropBox(),this},clear:function(){return this.cropped&&!this.disabled&&(g(this.cropBoxData,{left:0,top:0,width:0,height:0}),this.cropped=!1,this.renderCropBox(),this.limitCanvas(!0,!0),this.renderCanvas(),X(this.dragBox,n),v(this.cropBox,L)),this},replace:function(e){var t=1<arguments.length&&void 0!==arguments[1]&&arguments[1];return!this.disabled&&e&&(this.isImg&&(this.element.src=e),t?(this.url=e,this.image.src=e,this.ready&&(this.viewBoxImage.src=e,z(this.previews,function(t){t.getElementsByTagName("img")[0].src=e}))):(this.isImg&&(this.replaced=!0),this.options.data=null,this.uncreate(),this.load(e))),this},enable:function(){return this.ready&&this.disabled&&(this.disabled=!1,X(this.cropper,K)),this},disable:function(){return this.ready&&!this.disabled&&(this.disabled=!0,v(this.cropper,K)),this},destroy:function(){var t=this.element;return t[c]&&(t[c]=void 0,this.isImg&&this.replaced&&(t.src=this.originalUrl),this.uncreate()),this},move:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.canvasData,a=i.left,i=i.top;return this.moveTo(vt(t)?t:a+Number(t),vt(e)?e:i+Number(e))},moveTo:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.canvasData,a=!1;return t=Number(t),e=Number(e),this.ready&&!this.disabled&&this.options.movable&&(p(t)&&(i.left=t,a=!0),p(e)&&(i.top=e,a=!0),a)&&this.renderCanvas(!0),this},zoom:function(t,e){var i=this.canvasData;return t=Number(t),this.zoomTo(i.width*(t=t<0?1/(1-t):1+t)/i.naturalWidth,null,e)},zoomTo:function(t,e,i){var a,n,o,h=this.options,r=this.canvasData,s=r.width,c=r.height,d=r.naturalWidth,l=r.naturalHeight;if(0<=(t=Number(t))&&this.ready&&!this.disabled&&h.zoomable){h=d*t,l=l*t;if(!1===y(this.element,st,{ratio:t,oldRatio:s/d,originalEvent:i}))return this;i?(t=this.pointers,d=Wt(this.cropper),t=t&&Object.keys(t).length?(o=n=a=0,z(t,function(t){var e=t.startX,t=t.startY;a+=e,n+=t,o+=1}),{pageX:a/=o,pageY:n/=o}):{pageX:i.pageX,pageY:i.pageY},r.left-=(h-s)*((t.pageX-d.left-r.left)/s),r.top-=(l-c)*((t.pageY-d.top-r.top)/c)):u(e)&&p(e.x)&&p(e.y)?(r.left-=(h-s)*((e.x-r.left)/s),r.top-=(l-c)*((e.y-r.top)/c)):(r.left-=(h-s)/2,r.top-=(l-c)/2),r.width=h,r.height=l,this.renderCanvas(!0)}return this},rotate:function(t){return this.rotateTo((this.imageData.rotate||0)+Number(t))},rotateTo:function(t){return p(t=Number(t))&&this.ready&&!this.disabled&&this.options.rotatable&&(this.imageData.rotate=t%360,this.renderCanvas(!0,!0)),this},scaleX:function(t){var e=this.imageData.scaleY;return this.scale(t,p(e)?e:1)},scaleY:function(t){var e=this.imageData.scaleX;return this.scale(p(e)?e:1,t)},scale:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.imageData,a=!1;return t=Number(t),e=Number(e),this.ready&&!this.disabled&&this.options.scalable&&(p(t)&&(i.scaleX=t,a=!0),p(e)&&(i.scaleY=e,a=!0),a)&&this.renderCanvas(!0,!0),this},getData:function(){var i,a,t=0<arguments.length&&void 0!==arguments[0]&&arguments[0],e=this.options,n=this.imageData,o=this.canvasData,h=this.cropBoxData;return this.ready&&this.cropped?(i={x:h.left-o.left,y:h.top-o.top,width:h.width,height:h.height},a=n.width/n.naturalWidth,z(i,function(t,e){i[e]=t/a}),t&&(o=Math.round(i.y+i.height),h=Math.round(i.x+i.width),i.x=Math.round(i.x),i.y=Math.round(i.y),i.width=h-i.x,i.height=o-i.y)):i={x:0,y:0,width:0,height:0},e.rotatable&&(i.rotate=n.rotate||0),e.scalable&&(i.scaleX=n.scaleX||1,i.scaleY=n.scaleY||1),i},setData:function(t){var e,i=this.options,a=this.imageData,n=this.canvasData,o={};return this.ready&&!this.disabled&&u(t)&&(e=!1,i.rotatable&&p(t.rotate)&&t.rotate!==a.rotate&&(a.rotate=t.rotate,e=!0),i.scalable&&(p(t.scaleX)&&t.scaleX!==a.scaleX&&(a.scaleX=t.scaleX,e=!0),p(t.scaleY))&&t.scaleY!==a.scaleY&&(a.scaleY=t.scaleY,e=!0),e&&this.renderCanvas(!0,!0),i=a.width/a.naturalWidth,p(t.x)&&(o.left=t.x*i+n.left),p(t.y)&&(o.top=t.y*i+n.top),p(t.width)&&(o.width=t.width*i),p(t.height)&&(o.height=t.height*i),this.setCropBoxData(o)),this},getContainerData:function(){return this.ready?g({},this.containerData):{}},getImageData:function(){return this.sized?g({},this.imageData):{}},getCanvasData:function(){var e=this.canvasData,i={};return this.ready&&z(["left","top","width","height","naturalWidth","naturalHeight"],function(t){i[t]=e[t]}),i},setCanvasData:function(t){var e=this.canvasData,i=e.aspectRatio;return this.ready&&!this.disabled&&u(t)&&(p(t.left)&&(e.left=t.left),p(t.top)&&(e.top=t.top),p(t.width)?(e.width=t.width,e.height=t.width/i):p(t.height)&&(e.height=t.height,e.width=t.height*i),this.renderCanvas(!0)),this},getCropBoxData:function(){var t,e=this.cropBoxData;return(t=this.ready&&this.cropped?{left:e.left,top:e.top,width:e.width,height:e.height}:t)||{}},setCropBoxData:function(t){var e,i,a=this.cropBoxData,n=this.options.aspectRatio;return this.ready&&this.cropped&&!this.disabled&&u(t)&&(p(t.left)&&(a.left=t.left),p(t.top)&&(a.top=t.top),p(t.width)&&t.width!==a.width&&(e=!0,a.width=t.width),p(t.height)&&t.height!==a.height&&(i=!0,a.height=t.height),n&&(e?a.height=a.width/n:i&&(a.width=a.height*n)),this.renderCropBox()),this},getCroppedCanvas:function(){var t,e,i,a,n,o,h,r,s,c,d,l,p,m,u,g,f,v,w,b,y,x,M,C,D,B,k,O=0<arguments.length&&void 0!==arguments[0]?arguments[0]:{};return this.ready&&window.HTMLCanvasElement?(B=this.canvasData,u=this.image,l=this.imageData,a=B,v=O,g=l.aspectRatio,e=l.naturalWidth,n=l.naturalHeight,c=void 0===(c=l.rotate)?0:c,d=void 0===(d=l.scaleX)?1:d,l=void 0===(l=l.scaleY)?1:l,i=a.aspectRatio,r=a.naturalWidth,a=a.naturalHeight,h=void 0===(h=v.fillColor)?"transparent":h,p=void 0===(p=v.imageSmoothingEnabled)||p,m=void 0===(m=v.imageSmoothingQuality)?"low":m,o=void 0===(o=v.maxWidth)?1/0:o,k=void 0===(k=v.maxHeight)?1/0:k,t=void 0===(t=v.minWidth)?0:t,v=void 0===(v=v.minHeight)?0:v,w=document.createElement("canvas"),f=w.getContext("2d"),s=R({aspectRatio:i,width:o,height:k}),i=R({aspectRatio:i,width:t,height:v},"cover"),r=Math.min(s.width,Math.max(i.width,r)),s=Math.min(s.height,Math.max(i.height,a)),i=R({aspectRatio:g,width:o,height:k}),a=R({aspectRatio:g,width:t,height:v},"cover"),o=Math.min(i.width,Math.max(a.width,e)),k=Math.min(i.height,Math.max(a.height,n)),g=[-o/2,-k/2,o,k],w.width=Y(r),w.height=Y(s),f.fillStyle=h,f.fillRect(0,0,r,s),f.save(),f.translate(r/2,s/2),f.rotate(c*Math.PI/180),f.scale(d,l),f.imageSmoothingEnabled=p,f.imageSmoothingQuality=m,f.drawImage.apply(f,[u].concat(A(g.map(function(t){return Math.floor(Y(t))})))),f.restore(),t=w,this.cropped?(e=(v=this.getData(O.rounded)).x,i=v.y,a=v.width,n=v.height,1!=(o=t.width/Math.floor(B.naturalWidth))&&(e*=o,i*=o,a*=o,n*=o),h=R({aspectRatio:k=a/n,width:O.maxWidth||1/0,height:O.maxHeight||1/0}),r=R({aspectRatio:k,width:O.minWidth||0,height:O.minHeight||0},"cover"),c=(s=R({aspectRatio:k,width:O.width||(1!=o?t.width:a),height:O.height||(1!=o?t.height:n)})).width,d=s.height,c=Math.min(h.width,Math.max(r.width,c)),d=Math.min(h.height,Math.max(r.height,d)),p=(l=document.createElement("canvas")).getContext("2d"),l.width=Y(c),l.height=Y(d),p.fillStyle=O.fillColor||"transparent",p.fillRect(0,0,c,d),m=O.imageSmoothingEnabled,u=O.imageSmoothingQuality,p.imageSmoothingEnabled=void 0===m||m,u&&(p.imageSmoothingQuality=u),g=t.width,f=t.height,w=i,(v=e)<=-a||g<v?C=x=b=v=0:v<=0?(x=-v,v=0,C=b=Math.min(g,a+v)):v<=g&&(x=0,C=b=Math.min(a,g-v)),b<=0||w<=-n||f<w?D=M=y=w=0:w<=0?(M=-w,w=0,D=y=Math.min(f,n+w)):w<=f&&(M=0,D=y=Math.min(n,f-w)),B=[v,w,b,y],0<C&&0<D&&B.push(x*(k=c/a),M*k,C*k,D*k),p.drawImage.apply(p,[t].concat(A(B.map(function(t){return Math.floor(Y(t))})))),l):t):null},setAspectRatio:function(t){var e=this.options;return this.disabled||vt(t)||(e.aspectRatio=Math.max(0,t)||NaN,this.ready&&(this.initCropBox(),this.cropped)&&this.renderCropBox()),this},setDragMode:function(t){var e,i,a=this.options,n=this.dragBox,o=this.face;return this.ready&&!this.disabled&&(i=a.movable&&t===J,a.dragMode=t=(e=t===F)||i?t:_,w(n,d,t),r(n,Q,e),r(n,V,i),a.cropBoxMovable||(w(o,d,t),r(o,Q,e),r(o,V,i))),this}},Pt=h.Cropper,It=function(){function n(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:{},i=this,a=n;if(!(i instanceof a))throw new TypeError("Cannot call a class as a function");if(!t||!mt.test(t.tagName))throw new Error("The first argument is required and must be an <img> or <canvas> element.");this.element=t,this.options=g({},ut,u(e)&&e),this.cropped=!1,this.disabled=!1,this.pointers={},this.ready=!1,this.reloading=!1,this.replaced=!1,this.sized=!1,this.sizing=!1,this.init()}var t,e,i;return t=n,i=[{key:"noConflict",value:function(){return window.Cropper=Pt,n}},{key:"setDefaults",value:function(t){g(ut,u(t)&&t)}}],(e=[{key:"init",value:function(){var t,e=this.element,i=e.tagName.toLowerCase();if(!e[c]){if(e[c]=this,"img"===i){if(this.isImg=!0,t=e.getAttribute("src")||"",!(this.originalUrl=t))return;t=e.src}else"canvas"===i&&window.HTMLCanvasElement&&(t=e.toDataURL());this.load(t)}}},{key:"load",value:function(t){var e,i,a,n,o,h,r=this;t&&(this.url=t,this.imageData={},e=this.element,(i=this.options).rotatable||i.scalable||(i.checkOrientation=!1),i.checkOrientation&&window.ArrayBuffer?lt.test(t)?pt.test(t)?this.read((h=(h=t).replace(Xt,""),a=atob(h),h=new ArrayBuffer(a.length),z(n=new Uint8Array(h),function(t,e){n[e]=a.charCodeAt(e)}),h)):this.clone():(o=new XMLHttpRequest,h=this.clone.bind(this),this.reloading=!0,(this.xhr=o).onabort=h,o.onerror=h,o.ontimeout=h,o.onprogress=function(){o.getResponseHeader("content-type")!==ct&&o.abort()},o.onload=function(){r.read(o.response)},o.onloadend=function(){r.reloading=!1,r.xhr=null},i.checkCrossOrigin&&Lt(t)&&e.crossOrigin&&(t=zt(t)),o.open("GET",t,!0),o.responseType="arraybuffer",o.withCredentials="use-credentials"===e.crossOrigin,o.send()):this.clone())}},{key:"read",value:function(t){var e=this.options,i=this.imageData,a=Rt(t),n=0,o=1,h=1;1<a&&(this.url=function(t,e){for(var i=[],a=new Uint8Array(t);0<a.length;)i.push(Yt.apply(null,yt(a.subarray(0,8192)))),a=a.subarray(8192);return"data:".concat(e,";base64,").concat(btoa(i.join("")))}(t,ct),n=(t=function(t){var e=0,i=1,a=1;switch(t){case 2:i=-1;break;case 3:e=-180;break;case 4:a=-1;break;case 5:e=90,a=-1;break;case 6:e=90;break;case 7:e=90,i=-1;break;case 8:e=-90}return{rotate:e,scaleX:i,scaleY:a}}(a)).rotate,o=t.scaleX,h=t.scaleY),e.rotatable&&(i.rotate=n),e.scalable&&(i.scaleX=o,i.scaleY=h),this.clone()}},{key:"clone",value:function(){var t=this.element,e=this.url,i=t.crossOrigin,a=e,n=(this.options.checkCrossOrigin&&Lt(e)&&(i=i||"anonymous",a=zt(e)),this.crossOrigin=i,this.crossOriginUrl=a,document.createElement("img"));i&&(n.crossOrigin=i),n.src=a||e,n.alt=t.alt||"The image to crop",(this.image=n).onload=this.start.bind(this),n.onerror=this.stop.bind(this),v(n,Z),t.parentNode.insertBefore(n,t.nextSibling)}},{key:"start",value:function(){function t(t,e){g(a.imageData,{naturalWidth:t,naturalHeight:e,aspectRatio:t/e}),a.initialImageData=g({},a.imageData),a.sizing=!1,a.sized=!0,a.build()}var e,i,a=this,n=this.image,o=(n.onload=null,n.onerror=null,this.sizing=!0,h.navigator&&/(?:iPad|iPhone|iPod).*?AppleWebKit/i.test(h.navigator.userAgent));n.naturalWidth&&!o?t(n.naturalWidth,n.naturalHeight):(e=document.createElement("img"),i=document.body||document.documentElement,(this.sizingImage=e).onload=function(){t(e.width,e.height),o||i.removeChild(e)},e.src=n.src,o||(e.style.cssText="left:0;max-height:none!important;max-width:none!important;min-height:0!important;min-width:0!important;opacity:0;position:absolute;top:0;z-index:-1;",i.appendChild(e)))}},{key:"stop",value:function(){var t=this.image;t.onload=null,t.onerror=null,t.parentNode.removeChild(t),this.image=null}},{key:"build",value:function(){var t,e,i,a,n,o,h,r,s;this.sized&&!this.ready&&(t=this.element,e=this.options,i=this.image,a=t.parentNode,(n=document.createElement("div")).innerHTML='<div class="cropper-container" touch-action="none"><div class="cropper-wrap-box"><div class="cropper-canvas"></div></div><div class="cropper-drag-box"></div><div class="cropper-crop-box"><span class="cropper-view-box"></span><span class="cropper-dashed dashed-h"></span><span class="cropper-dashed dashed-v"></span><span class="cropper-center"></span><span class="cropper-face"></span><span class="cropper-line line-e" data-cropper-action="e"></span><span class="cropper-line line-n" data-cropper-action="n"></span><span class="cropper-line line-w" data-cropper-action="w"></span><span class="cropper-line line-s" data-cropper-action="s"></span><span class="cropper-point point-e" data-cropper-action="e"></span><span class="cropper-point point-n" data-cropper-action="n"></span><span class="cropper-point point-w" data-cropper-action="w"></span><span class="cropper-point point-s" data-cropper-action="s"></span><span class="cropper-point point-ne" data-cropper-action="ne"></span><span class="cropper-point point-nw" data-cropper-action="nw"></span><span class="cropper-point point-sw" data-cropper-action="sw"></span><span class="cropper-point point-se" data-cropper-action="se"></span></div></div>',o=(n=n.querySelector(".".concat(c,"-container"))).querySelector(".".concat(c,"-canvas")),h=n.querySelector(".".concat(c,"-drag-box")),s=(r=n.querySelector(".".concat(c,"-crop-box"))).querySelector(".".concat(c,"-face")),this.container=a,this.cropper=n,this.canvas=o,this.dragBox=h,this.cropBox=r,this.viewBox=n.querySelector(".".concat(c,"-view-box")),this.face=s,o.appendChild(i),v(t,L),a.insertBefore(n,t.nextSibling),X(i,Z),this.initPreview(),this.bind(),e.initialAspectRatio=Math.max(0,e.initialAspectRatio)||NaN,e.aspectRatio=Math.max(0,e.aspectRatio)||NaN,e.viewMode=Math.max(0,Math.min(3,Math.round(e.viewMode)))||0,v(r,L),e.guides||v(r.getElementsByClassName("".concat(c,"-dashed")),L),e.center||v(r.getElementsByClassName("".concat(c,"-center")),L),e.background&&v(n,"".concat(c,"-bg")),e.highlight||v(s,G),e.cropBoxMovable&&(v(s,V),w(s,d,I)),e.cropBoxResizable||(v(r.getElementsByClassName("".concat(c,"-line")),L),v(r.getElementsByClassName("".concat(c,"-point")),L)),this.render(),this.ready=!0,this.setDragMode(e.dragMode),e.autoCrop&&this.crop(),this.setData(e.data),l(e.ready)&&b(t,"ready",e.ready,{once:!0}),y(t,"ready"))}},{key:"unbuild",value:function(){var t;this.ready&&(this.ready=!1,this.unbind(),this.resetPreview(),(t=this.cropper.parentNode)&&t.removeChild(this.cropper),X(this.element,L))}},{key:"uncreate",value:function(){this.ready?(this.unbuild(),this.ready=!1,this.cropped=!1):this.sizing?(this.sizingImage.onload=null,this.sizing=!1,this.sized=!1):this.reloading?(this.xhr.onabort=null,this.xhr.abort()):this.image&&this.stop()}}])&&j(t.prototype,e),i&&j(t,i),Object.defineProperty(t,"prototype",{writable:!1}),n}();return g(It.prototype,t,i,e,St,jt,At),It});
</script>
<script>
/* ---- string-art-core.js (shared algorithm, same file used by the standalone client and the CLI's test suite) ---- */
/*
 * String Art Core (browser + Node-testable)
 * ==========================================
 * Pure, DOM-free functions for the greedy nail-and-thread algorithm used by
 * index.html. Kept dependency-free so the exact same file can be loaded as a
 * plain script tag in the browser, or required() from a plain Node test
 * script (see test-core.js), without the two ever drifting apart.
 */
(function (root) {
  "use strict";

  // ---- Pin geometry ----------------------------------------------------

  // Pin 0 at angle 0 (three o'clock), going clockwise in canvas coordinates
  // (y grows downward), evenly spaced. Matches the Python generator so a
  // sequence means the same physical nail regardless of which tool made it.
  function pinPositions(numPins, size) {
    const cx = (size - 1) / 2;
    const cy = (size - 1) / 2;
    const r = size / 2 - 1;
    const pins = new Array(numPins);
    for (let i = 0; i < numPins; i++) {
      const theta = (2 * Math.PI * i) / numPins;
      pins[i] = { x: cx + r * Math.cos(theta), y: cy + r * Math.sin(theta) };
    }
    return pins;
  }

  // ---- Image -> residual/error array ------------------------------------

  // RGBA Uint8ClampedArray -> Float32Array of luma (ITU-R BT.601 weights).
  function toGrayscale(rgba, size) {
    const out = new Float32Array(size * size);
    for (let i = 0, p = 0; i < out.length; i++, p += 4) {
      out[i] = 0.299 * rgba[p] + 0.587 * rgba[p + 1] + 0.114 * rgba[p + 2];
    }
    return out;
  }

  // Zero out any contribution from outside the inscribed circle by pinning
  // it to whichever brightness makes that pixel's later residual 0.
  function maskOutsideCircle(brightness, size, mode) {
    const cx = (size - 1) / 2;
    const cy = (size - 1) / 2;
    const r = size / 2;
    const fill = mode === "dark" ? 0 : 255;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > r * r) brightness[y * size + x] = fill;
      }
    }
    return brightness;
  }

  // Light mode targets dark regions (error = 255 - brightness); dark mode
  // targets bright regions (error = brightness) -- e.g. a light thread on a
  // dark board.
  function toResidual(brightness, mode) {
    const out = new Float32Array(brightness.length);
    if (mode === "dark") {
      out.set(brightness);
    } else {
      for (let i = 0; i < brightness.length; i++) out[i] = 255 - brightness[i];
    }
    return out;
  }

  // ---- Candidate line precompute ----------------------------------------

  // For every pin pair at least `minSep` indices apart (skips near-duplicate,
  // near-zero-length chords), precompute the flat pixel indices (y*size+x)
  // the connecting line passes through. Keyed "i_j" with i < j.
  function precomputeLines(pins, size, minSep) {
    const n = pins.length;
    const map = new Map();
    for (let i = 0; i < n; i++) {
      for (let j = i + 1; j < n; j++) {
        const sep = Math.min(j - i, n - (j - i));
        if (sep < minSep) continue;
        const p0 = pins[i], p1 = pins[j];
        const dx = p1.x - p0.x, dy = p1.y - p0.y;
        const dist = Math.hypot(dx, dy);
        const steps = Math.max(2, Math.round(dist));
        const idx = new Int32Array(steps);
        let count = 0;
        let lastFlat = -1;
        for (let k = 0; k < steps; k++) {
          const t = k / (steps - 1);
          let x = Math.round(p0.x + t * dx);
          let y = Math.round(p0.y + t * dy);
          if (x < 0) x = 0; else if (x >= size) x = size - 1;
          if (y < 0) y = 0; else if (y >= size) y = size - 1;
          const flat = y * size + x;
          if (flat !== lastFlat) {
            idx[count++] = flat;
            lastFlat = flat;
          }
        }
        map.set(i + "_" + j, count === steps ? idx : idx.subarray(0, count));
      }
    }
    return map;
  }

  function lineKey(a, b) {
    return a < b ? a + "_" + b : b + "_" + a;
  }

  // ---- Greedy generation (chunkable, so the caller can yield to the UI) --

  function createGenerationState(residual, numPins, startPin, recentLimit) {
    return {
      residual,               // Float32Array, mutated in place as lines are "drawn"
      numPins,
      current: startPin,
      sequence: [startPin],
      recent: [],
      recentLimit: Math.max(4, recentLimit),
      done: false,
    };
  }

  // Advances the greedy search by up to `count` more chords. Returns the
  // pin indices newly added to the sequence this call (for incremental
  // rendering) and mutates `state` in place. Sets state.done when no
  // improving chord remains or `maxChords` total has been reached.
  function stepChunk(state, lineMap, weight, count, maxChords) {
    const added = [];
    for (let k = 0; k < count && state.sequence.length - 1 < maxChords; k++) {
      let bestPin = -1, bestScore = -1, bestIdx = null;
      for (let j = 0; j < state.numPins; j++) {
        if (j === state.current || state.recent.indexOf(j) !== -1) continue;
        const idx = lineMap.get(lineKey(state.current, j));
        if (!idx) continue;
        let sum = 0;
        for (let m = 0; m < idx.length; m++) sum += state.residual[idx[m]];
        const score = sum / idx.length;
        if (score > bestScore) { bestScore = score; bestPin = j; bestIdx = idx; }
      }
      if (bestPin === -1 || bestScore <= 0) { state.done = true; break; }

      for (let m = 0; m < bestIdx.length; m++) {
        const v = state.residual[bestIdx[m]] - weight;
        state.residual[bestIdx[m]] = v > 0 ? v : 0;
      }
      state.sequence.push(bestPin);
      added.push(bestPin);
      state.recent.push(bestPin);
      if (state.recent.length > state.recentLimit) state.recent.shift();
      state.current = bestPin;
    }
    if (state.sequence.length - 1 >= maxChords) state.done = true;
    return added;
  }

  // ---- Export helpers ----------------------------------------------------

  function formatStepsText(sequence) {
    let out = "String Art Steps (" + (sequence.length - 1) + " chords, " + sequence.length + " nail stops)\n";
    for (let i = 1; i < sequence.length; i++) {
      out += i + ": From Pin " + sequence[i - 1] + " to Pin " + sequence[i] + "\n";
    }
    return out;
  }

  function formatSequenceText(sequence) {
    return sequence.join("\n") + "\n";
  }

  function svgFromSequence(pins, sequence, size, bgColor, strokeColor, strokeWidth) {
    let svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + size + " " + size +
      '" width="' + size + '" height="' + size + '">';
    svg += '<rect width="100%" height="100%" fill="' + bgColor + '"/>';
    svg += '<g fill="none" stroke="' + strokeColor + '" stroke-width="' + strokeWidth + '" stroke-linecap="round">';
    for (let i = 1; i < sequence.length; i++) {
      const a = pins[sequence[i - 1]], b = pins[sequence[i]];
      svg += '<line x1="' + a.x.toFixed(2) + '" y1="' + a.y.toFixed(2) +
        '" x2="' + b.x.toFixed(2) + '" y2="' + b.y.toFixed(2) + '"/>';
    }
    svg += "</g></svg>";
    return svg;
  }

  const api = {
    pinPositions,
    toGrayscale,
    maskOutsideCircle,
    toResidual,
    precomputeLines,
    lineKey,
    createGenerationState,
    stepChunk,
    formatStepsText,
    formatSequenceText,
    svgFromSequence,
  };

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api; // Node (tests)
  } else {
    root.StringArtCore = api; // browser <script> tag
  }
})(typeof window !== "undefined" ? window : globalThis);

</script>
<script>
(function () {
  "use strict";
  const C = window.StringArtCore;
  const WORK_SIZE = 600; // internal square working resolution, px

  const canvas = document.getElementById("artCanvas");
  const ctx = canvas.getContext("2d");
  canvas.width = WORK_SIZE;
  canvas.height = WORK_SIZE;

  const fileInput = document.getElementById("fileInput");
  const fileBtnLabel = document.getElementById("fileBtnLabel");
  const dropHint = document.getElementById("dropHint");
  const darkModeToggle = document.getElementById("darkMode");
  const numPinsInput = document.getElementById("numPins");
  const numChordsInput = document.getElementById("numChords");
  const lineWeightInput = document.getElementById("lineWeight");
  const generateBtn = document.getElementById("generateBtn");
  const progressTrack = document.getElementById("progressTrack");
  const progressFill = document.getElementById("progressFill");
  const progressLabel = document.getElementById("progressLabel");
  const statLine = document.getElementById("statLine");
  const downloadStepsBtn = document.getElementById("downloadStepsBtn");
  const downloadSvgBtn = document.getElementById("downloadSvgBtn");
  const sendBtn = document.getElementById("sendBtn");
  const sendMsg = document.getElementById("sendMsg");

  const cropOverlay = document.getElementById("cropOverlay");
  const cropImage = document.getElementById("cropImage");
  const cropCancelBtn = document.getElementById("cropCancelBtn");
  const cropConfirmBtn = document.getElementById("cropConfirmBtn");

  let croppedCanvas = null;
  let pins = null;
  let lastResult = null; // { sequence, numPins }
  let cropper = null;
  let generating = false;

  ["numChords", "lineWeight"].forEach((id) => {
    const el = document.getElementById(id);
    const out = document.getElementById(id + "Val");
    el.addEventListener("input", () => { out.textContent = el.value; });
  });

  // Nail count is shared, so this slider both reads and writes NailCount
  // rather than owning a value of its own.
  const numPinsVal = document.getElementById("numPinsVal");
  numPinsInput.addEventListener("input", () => NailCount.set(numPinsInput.value, "design"));
  NailCount.on((v, src) => {
    numPinsVal.textContent = v;
    if (src !== "design") numPinsInput.value = v;
  });

  function bg() { return darkModeToggle.checked ? "#262626" : "#ffffff"; }
  function stroke() { return darkModeToggle.checked ? "rgba(255,255,255,0.55)" : "rgba(0,0,0,0.55)"; }
  function paintBackground() { ctx.fillStyle = bg(); ctx.fillRect(0, 0, canvas.width, canvas.height); }

  darkModeToggle.addEventListener("change", () => {
    if (!croppedCanvas) { paintBackground(); return; }
    if (lastResult) runGeneration();
  });

  // ---- Image select -> crop modal ----------------------------------------

  fileInput.addEventListener("change", () => {
    const f = fileInput.files[0];
    if (!f) return;
    const reader = new FileReader();
    reader.onload = (e) => {
      cropImage.src = e.target.result;
      cropOverlay.hidden = false;
      if (cropper) cropper.destroy();
      cropper = new Cropper(cropImage, { aspectRatio: 1, viewMode: 1, autoCropArea: 1, background: false });
    };
    reader.readAsDataURL(f);
  });

  cropCancelBtn.addEventListener("click", () => {
    cropOverlay.hidden = true;
    if (cropper) { cropper.destroy(); cropper = null; }
  });

  cropConfirmBtn.addEventListener("click", () => {
    const srcCanvas = cropper.getCroppedCanvas({ width: WORK_SIZE, height: WORK_SIZE });
    cropOverlay.hidden = true;
    cropper.destroy(); cropper = null;

    croppedCanvas = srcCanvas;
    fileBtnLabel.textContent = fileInput.files[0] ? fileInput.files[0].name : "Choose a photo…";
    dropHint.hidden = true;

    const size = WORK_SIZE;
    const c = document.createElement("canvas");
    c.width = size; c.height = size;
    const cctx = c.getContext("2d");
    cctx.drawImage(srcCanvas, 0, 0, size, size);
    const imgData = cctx.getImageData(0, 0, size, size);
    const gray = C.toGrayscale(imgData.data, size);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    paintBackground();
    const out = ctx.getImageData(0, 0, size, size);
    for (let i = 0, p = 0; i < gray.length; i++, p += 4) {
      const v = gray[i];
      out.data[p] = v; out.data[p + 1] = v; out.data[p + 2] = v; out.data[p + 3] = 255;
    }
    ctx.putImageData(out, 0, 0);

    generateBtn.disabled = false;
    lastResult = null;
    downloadStepsBtn.disabled = true;
    downloadSvgBtn.disabled = true;
    cardPdfBtn.disabled = true;
    sendBtn.disabled = true;
  });

  // ---- Generation ---------------------------------------------------------

  function setProgress(pct, label) {
    progressTrack.hidden = false;
    progressLabel.hidden = false;
    progressFill.style.width = Math.max(0, Math.min(100, pct)) + "%";
    progressLabel.textContent = label;
  }

  generateBtn.addEventListener("click", runGeneration);

  function runGeneration() {
    if (generating || !croppedCanvas) return;
    generating = true;
    generateBtn.disabled = true;
    downloadStepsBtn.disabled = true;
    downloadSvgBtn.disabled = true;
    cardPdfBtn.disabled = true;
    sendBtn.disabled = true;

    const numPins = NailCount.get();
    const numChords = parseInt(numChordsInput.value, 10);
    const weight = parseFloat(lineWeightInput.value);
    const mode = darkModeToggle.checked ? "dark" : "light";
    const size = WORK_SIZE;

    const c = document.createElement("canvas");
    c.width = size; c.height = size;
    const cctx = c.getContext("2d");
    cctx.drawImage(croppedCanvas, 0, 0, size, size);
    const imgData = cctx.getImageData(0, 0, size, size);

    const brightness = C.toGrayscale(imgData.data, size);
    C.maskOutsideCircle(brightness, size, mode);
    const residual = C.toResidual(brightness, mode);

    pins = C.pinPositions(numPins, size);
    setProgress(0, "Precomputing candidate lines…");

    requestAnimationFrame(() => {
      const minSep = Math.max(2, Math.round(numPins / 20));
      const lineMap = C.precomputeLines(pins, size, minSep);
      const state = C.createGenerationState(residual, numPins, 0, Math.max(4, Math.round(numPins / 20)));

      paintBackground();
      ctx.strokeStyle = stroke();
      ctx.lineWidth = 0.6;

      const CHUNK = 25;
      function tick() {
        const added = C.stepChunk(state, lineMap, weight, CHUNK, numChords);
        ctx.beginPath();
        for (let k = 0; k < added.length; k++) {
          const idx = state.sequence.length - added.length + k;
          const a = pins[state.sequence[idx - 1]], b = pins[state.sequence[idx]];
          ctx.moveTo(a.x, a.y);
          ctx.lineTo(b.x, b.y);
        }
        ctx.stroke();

        const placed = state.sequence.length - 1;
        setProgress((placed / numChords) * 100, placed + " / " + numChords + " chords");

        if (!state.done && placed < numChords) {
          setTimeout(tick, 0);
        } else {
          finishGeneration(state.sequence, numPins);
        }
      }
      tick();
    });
  }

  function finishGeneration(sequence, numPins) {
    generating = false;
    generateBtn.disabled = false;
    lastResult = { sequence, numPins };
    setProgress(100, "Done — " + (sequence.length - 1) + " chords");
    statLine.hidden = false;
    statLine.textContent = "pins: " + numPins + "    chords: " + (sequence.length - 1);
    downloadStepsBtn.disabled = false;
    downloadSvgBtn.disabled = false;
    cardPdfBtn.disabled = false;
    sendBtn.disabled = false;
  }

  function triggerDownload(filename, content, type) {
    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  downloadStepsBtn.addEventListener("click", () => {
    if (!lastResult) return;
    triggerDownload("string_art_steps.txt", C.formatStepsText(lastResult.sequence), "text/plain;charset=utf-8");
  });

  // ---- Greeting card (A4 landscape PDF) ---------------------------------
  //
  // A PDF can carry a JPEG's bytes verbatim as a DCTDecode stream, so the whole
  // writer is about sixty lines. jsPDF would do the same job for roughly 300 KB
  // of PROGMEM, which the ESP8266 has not got to spare.

  const cardTitle = document.getElementById("cardTitle");
  const cardMsg = document.getElementById("cardMsg");
  const cardFold = document.getElementById("cardFold");
  const cardPdfBtn = document.getElementById("cardPdfBtn");
  const cardMsgLine = document.getElementById("cardMsgLine");

  const CARD_DPI = 150;                       // 1754 x 1240 px, ~9 MB of canvas
  const A4_W_PT = 842, A4_H_PT = 595;         // A4 landscape in PDF points

  function drawCard() {
    const W = Math.round(297 / 25.4 * CARD_DPI);
    const H = Math.round(210 / 25.4 * CARD_DPI);
    const c = document.createElement("canvas");
    c.width = W; c.height = H;
    const g = c.getContext("2d");

    g.fillStyle = "#ffffff";
    g.fillRect(0, 0, W, H);

    const half = W / 2;
    // A landscape sheet folded down the middle keeps both halves upright, so
    // nothing needs rotating: right half is the cover, left half the inside.
    if (cardFold.checked) {
      g.save();
      g.strokeStyle = "#d8d2c8"; g.lineWidth = 1.5; g.setLineDash([9, 9]);
      g.beginPath(); g.moveTo(half, 40); g.lineTo(half, H - 40); g.stroke();
      g.restore();
    }

    // --- cover: the string art ---
    const artSide = Math.min(half - 150, H - 300);
    const ax = half + (half - artSide) / 2;
    const ay = (H - artSide) / 2 - 40;
    g.drawImage(canvas, ax, ay, artSide, artSide);

    const title = cardTitle.value.trim();
    g.fillStyle = "#1a1a1a";
    g.textAlign = "center";
    if (title) {
      g.font = "600 " + Math.round(CARD_DPI * 0.26) + "px Georgia, 'Times New Roman', serif";
      g.fillText(title, half + half / 2, ay + artSide + CARD_DPI * 0.52, half - 120);
    }
    if (lastResult) {
      g.fillStyle = "#8a8378";
      g.font = Math.round(CARD_DPI * 0.10) + "px Georgia, serif";
      g.fillText(lastResult.numPins + " nails  \u00b7  " +
                 (lastResult.sequence.length - 1) + " chords",
                 half + half / 2, H - CARD_DPI * 0.42);
    }

    // --- inside: the photo it came from, and the message ---
    if (croppedCanvas) {
      const ps = Math.min(half - 320, H - 560);
      const px = (half - ps) / 2, py = CARD_DPI * 0.75;
      g.save();
      g.beginPath(); g.arc(px + ps / 2, py + ps / 2, ps / 2, 0, Math.PI * 2);
      g.clip();
      g.drawImage(croppedCanvas, px, py, ps, ps);
      g.restore();
      g.strokeStyle = "#ddd6cb"; g.lineWidth = 2;
      g.beginPath(); g.arc(px + ps / 2, py + ps / 2, ps / 2, 0, Math.PI * 2); g.stroke();

      const msg = cardMsg.value.trim();
      if (msg) {
        g.fillStyle = "#2a2a2a";
        g.font = Math.round(CARD_DPI * 0.155) + "px Georgia, 'Times New Roman', serif";
        const maxW = half - 260;
        let y = py + ps + CARD_DPI * 0.55;
        for (const para of msg.split("\n")) {
          let line = "";
          for (const word of para.split(/\s+/)) {
            const test = line ? line + " " + word : word;
            if (g.measureText(test).width > maxW && line) {
              g.fillText(line, half / 2, y); y += CARD_DPI * 0.24; line = word;
            } else { line = test; }
          }
          if (line) { g.fillText(line, half / 2, y); y += CARD_DPI * 0.24; }
        }
      }
    }
    return c;
  }

  function jpegToPdf(jpeg, imgW, imgH, pageW, pageH) {
    const enc = (t) => new TextEncoder().encode(t);
    const parts = []; const offs = []; let len = 0;
    const push = (u8) => { parts.push(u8); len += u8.length; };
    const str = (t) => push(enc(t));
    const mark = (n) => { offs[n] = len; };

    push(new Uint8Array([0x25,0x50,0x44,0x46,0x2d,0x31,0x2e,0x34,0x0a,
                         0x25,0xe2,0xe3,0xcf,0xd3,0x0a]));
    mark(1); str("1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n");
    mark(2); str("2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n");
    mark(3); str("3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + pageW + " " +
                 pageH + "]/Resources<</XObject<</Im0 4 0 R>>>>/Contents 5 0 R>>\nendobj\n");
    mark(4); str("4 0 obj\n<</Type/XObject/Subtype/Image/Width " + imgW +
                 "/Height " + imgH + "/ColorSpace/DeviceRGB/BitsPerComponent 8" +
                 "/Filter/DCTDecode/Length " + jpeg.length + ">>\nstream\n");
    push(jpeg);
    str("\nendstream\nendobj\n");
    const content = "q " + pageW + " 0 0 " + pageH + " 0 0 cm /Im0 Do Q\n";
    mark(5); str("5 0 obj\n<</Length " + content.length + ">>\nstream\n" +
                 content + "endstream\nendobj\n");

    const xref = len;
    let x = "xref\n0 6\n0000000000 65535 f \n";
    for (let i = 1; i <= 5; i++) x += String(offs[i]).padStart(10, "0") + " 00000 n \n";
    x += "trailer\n<</Size 6/Root 1 0 R>>\nstartxref\n" + xref + "\n%%EOF\n";
    str(x);

    const out = new Uint8Array(len); let at = 0;
    for (const p of parts) { out.set(p, at); at += p.length; }
    return out;
  }

  cardPdfBtn.addEventListener("click", async () => {
    if (!lastResult) return;
    cardPdfBtn.disabled = true;
    const was = cardPdfBtn.textContent;
    cardPdfBtn.textContent = "Building\u2026";
    try {
      const c = drawCard();
      const blob = await new Promise((res) => c.toBlob(res, "image/jpeg", 0.92));
      const jpeg = new Uint8Array(await blob.arrayBuffer());
      const pdf = jpegToPdf(jpeg, c.width, c.height, A4_W_PT, A4_H_PT);
      triggerDownload("string_art_card.pdf", pdf, "application/pdf");
      cardMsgLine.textContent =
        "A4 landscape, " + CARD_DPI + " dpi, " + Math.round(pdf.length / 1024) +
        " kB. Print at 100% scale, not fit-to-page.";
    } catch (err) {
      cardMsgLine.textContent = "Could not build the PDF: " + err.message;
    }
    cardPdfBtn.textContent = was;
    cardPdfBtn.disabled = false;
  });

  downloadSvgBtn.addEventListener("click", () => {
    if (!lastResult) return;
    const svg = C.svgFromSequence(pins, lastResult.sequence, WORK_SIZE, bg(),
      darkModeToggle.checked ? "#ffffff" : "#000000", 0.6);
    triggerDownload("string_art.svg", svg, "image/svg+xml;charset=utf-8");
  });

  // ---- Send to THIS machine (same origin -- no address, no CORS needed) --

  sendBtn.addEventListener("click", async () => {
    sendMsg.className = "msg";
    if (!lastResult) { sendMsg.textContent = "Generate a sequence first."; sendMsg.className = "msg show err"; return; }

    sendBtn.disabled = true;
    sendBtn.textContent = "Sending…";
    try {
      await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "numNails=" + lastResult.numPins,
      });
      const r = await fetch("/upload", {
        method: "POST",
        headers: { "Content-Type": "text/plain" },
        body: lastResult.sequence.join(","),
      });
      if (!r.ok) throw new Error("HTTP " + r.status);
      sendMsg.textContent = "Sequence loaded — " + lastResult.sequence.length + " nail stops. Use Wrap below to start.";
      sendMsg.className = "msg show ok";
      refreshIndexer();
    } catch (err) {
      sendMsg.textContent = "Could not save the sequence to the machine. Try again.";
      sendMsg.className = "msg show err";
    } finally {
      sendBtn.disabled = false;
      sendBtn.textContent = "Send to this machine";
    }
  });

  // ---- Wrap section (indexer controls, same device, relative fetches) ----

  const idxNailNum = document.getElementById("idxNailNum");
  const idxProgressText = document.getElementById("idxProgressText");
  const idxEta = document.getElementById("idxEta");
  const etaElapsed = document.getElementById("etaElapsed");
  const etaLeft = document.getElementById("etaLeft");
  const etaClock = document.getElementById("etaClock");
  const etaPer = document.getElementById("etaPer");

  function hms(ms){
    if (!isFinite(ms) || ms < 0) return "--";
    const t = Math.round(ms / 1000);
    const h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60), s = t % 60;
    return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0");
  }

  function renderEta(j){
    const left = Math.max(0, j.total - j.currentIndex - 1);
    if (!j.total) { idxEta.hidden = true; return; }
    idxEta.hidden = false;
    etaElapsed.textContent = hms(j.elapsedMs);
    if (!j.avgNailMs) {
      // Nothing measured yet. Guessing from the configured timings would
      // ignore disc travel and read low, so say so instead.
      etaLeft.textContent = "measuring";
      etaClock.textContent = "--";
      etaPer.textContent = left + " nails left";
      return;
    }
    const remain = left * j.avgNailMs;
    etaLeft.textContent = hms(remain);
    const done = new Date(Date.now() + remain);
    const sameDay = done.toDateString() === new Date().toDateString();
    etaClock.textContent = done.toTimeString().slice(0, 5) +
      (sameDay ? "" : " +" + Math.ceil((done - new Date()) / 86400000) + "d");
    etaPer.textContent = (j.avgNailMs / 1000).toFixed(1) + " s";
  }
  const idxBar = document.getElementById("idxBar");
  const idxAutoBtn = document.getElementById("idxAutoBtn");
  const idxNumNails = document.getElementById("idxNumNails");
  const idxStepDelay = document.getElementById("idxStepDelay");
  const idxAutoMs = document.getElementById("idxAutoMs");
  const idxFindHomeBtn = document.getElementById("idxFindHomeBtn");
  const idxSwitchText = document.getElementById("idxSwitchText");
  const idxReverseDir = document.getElementById("idxReverseDir");
  const idxDirTestBtn = document.getElementById("idxDirTestBtn");
  const idxDirTestText = document.getElementById("idxDirTestText");
  const idxGotoStepVal = document.getElementById("idxGotoStepVal");
  const idxGotoStepBtn = document.getElementById("idxGotoStepBtn");
  const idxGotoStepHint = document.getElementById("idxGotoStepHint");
  const idxResumeBanner = document.getElementById("idxResumeBanner");
  const idxAutoHome = document.getElementById("idxAutoHome");
  const calJogUnit = document.getElementById("calJogUnit");
  const calNailVal = document.getElementById("calNailVal");
  const calSetBtn = document.getElementById("calSetBtn");
  const calOffsetText = document.getElementById("calOffsetText");
  const calRevsVal = document.getElementById("calRevsVal");
  const calMoveBtn = document.getElementById("calMoveBtn");
  const calErrVal = document.getElementById("calErrVal");
  const calReportBtn = document.getElementById("calReportBtn");
  const calStepsText = document.getElementById("calStepsText");
  const calRehome = document.getElementById("calRehome");
  const calRehomeText = document.getElementById("calRehomeText");
  const wrapMode = document.getElementById("wrapMode");
  const wrapSteps = document.getElementById("wrapSteps");
  const wrapDirFlip = document.getElementById("wrapDirFlip");
  const wrapBreakdown = document.getElementById("wrapBreakdown");
  const wrapTestBtn = document.getElementById("wrapTestBtn");
  const wrapTestCwBtn = document.getElementById("wrapTestCwBtn");
  const wrapTestCcwBtn = document.getElementById("wrapTestCcwBtn");
  const wrapTestText = document.getElementById("wrapTestText");
  const wrapSweep = document.getElementById("wrapSweep");
  const wrapHoldInMs = document.getElementById("wrapHoldInMs");
  const wrapHoldSweepMs = document.getElementById("wrapHoldSweepMs");
  const servoSlewDeg = document.getElementById("servoSlewDeg");
  const servoSlewMs = document.getElementById("servoSlewMs");
  const restVal = document.getElementById("restVal");
  const feedVal = document.getElementById("feedVal");
  const servoGoRest = document.getElementById("servoGoRest");
  const servoGoFeed = document.getElementById("servoGoFeed");
  const servoSwing = document.getElementById("servoSwing");
  const servoLiveText = document.getElementById("servoLiveText");
  const wrapStepsNails = document.getElementById("wrapStepsNails");
  const wrapSweepNails = document.getElementById("wrapSweepNails");
  const wrapRecalcBtn = document.getElementById("wrapRecalcBtn");
  const feederAutoFeed = document.getElementById("feederAutoFeed");
  const feederFeedBtn = document.getElementById("feederFeedBtn");
  const feederRestAngle = document.getElementById("feederRestAngle");
  const feederFeedAngle = document.getElementById("feederFeedAngle");
  const feederPulseMs = document.getElementById("feederPulseMs");
  const feederSettleMs = document.getElementById("feederSettleMs");
  const feederRecoverMs = document.getElementById("feederRecoverMs");
  const feederCycleText = document.getElementById("feederCycleText");
  const cycleBreakdown = document.getElementById("cycleBreakdown");
  const advSaveBtn = document.getElementById("advSaveBtn");
  const advResetBtn = document.getElementById("advResetBtn");
  const advMsg = document.getElementById("advMsg");
  let idxAutoOn = false;

  // ---- Edit guard ------------------------------------------------------
  // /status is polled once a second. Writing every reply straight into the
  // inputs meant a field you were halfway through typing got reset under your
  // fingers -- which is why a longer settle time appeared not to take effect.
  // A field goes "dirty" on first keystroke and is left alone until it is
  // saved or the panel is reset.
  const advFields = [idxNumNails, idxStepDelay, idxAutoMs, feederRestAngle,
                     feederFeedAngle, feederPulseMs, feederSettleMs, feederRecoverMs,
                     wrapSteps, wrapSweep, wrapHoldInMs, wrapHoldSweepMs,
                     servoSlewDeg, servoSlewMs, calRehome];
  const dirty = new Set();
  advFields.forEach(f => f.addEventListener("input", () => {
    dirty.add(f.id);
    advSaveBtn.textContent = "Save advanced settings *";
    updateCycleBreakdown();
  }));
  function clearDirty(){
    dirty.clear();
    advSaveBtn.textContent = "Save advanced settings";
  }
  // Only writes the machine's value into a field the user is not editing.
  function syncField(input, value){
    if (dirty.has(input.id) || document.activeElement === input) return;
    if (input.value !== String(value)) input.value = value;
  }
  function showAdvMsg(text, ok){
    advMsg.textContent = text;
    advMsg.className = "msg show " + (ok ? "ok" : "err");
    setTimeout(() => { advMsg.className = "msg"; }, 2500);
  }

  // Reads straight from the inputs so the numbers update as you type, before
  // anything is saved.
  function updateCycleBreakdown(){
    const settle = +feederSettleMs.value || 0;
    const pulse = +feederPulseMs.value || 0;
    const recover = +feederRecoverMs.value || 0;
    const dwell = +idxAutoMs.value || 0;
    const feed = settle + pulse + recover;
    cycleBreakdown.className = "sync-box";
    cycleBreakdown.innerHTML =
      '<b>' + (settle/1000).toFixed(2) + ' s</b> settle <span class="dim">&rarr;</span> ' +
      '<b>' + (pulse/1000).toFixed(2) + ' s</b> pulse <span class="dim">&rarr;</span> ' +
      '<b>' + (recover/1000).toFixed(2) + ' s</b> recover<br>' +
      '<span class="dim">feed cycle ' + (feed/1000).toFixed(2) + ' s, then ' +
      (dwell/1000).toFixed(1) + ' s dwell &mdash; ' +
      ((feed + dwell)/1000).toFixed(2) + ' s per nail plus the move.</span>' +
      (settle < 300
        ? '<br><b>Settle under 0.3 s</b> &mdash; the disc is usually still moving.'
        : '');
    feederCycleText.textContent = "Feed cycle " + (feed/1000).toFixed(2) + " s per nail.";
  }

  async function refreshIndexer() {
    try {
      const r = await fetch("/status");
      const j = await r.json();
      idxNailNum.textContent = j.currentNail;
      idxProgressText.textContent = j.homing
        ? "homing…"
        : j.feederBusy
          ? "feeding thread…"
          : "step " + j.currentIndex + " / " + j.total + "  (next: nail " + j.nextNail + ")";
      idxBar.value = j.total ? (100 * j.currentIndex / j.total) : 0;
      NailCount.adopt(j.numNails);
      idxGotoStepHint.textContent = j.total
        ? "0 to " + (j.total - 1) + ", currently on " + j.currentIndex + "."
        : "No sequence loaded yet.";
      if (document.activeElement !== idxAutoHome) idxAutoHome.checked = j.autoHomeOnBoot;
      if (document.activeElement !== wrapMode) wrapMode.checked = j.wrapMode;
      if (document.activeElement !== wrapDirFlip) wrapDirFlip.checked = (j.wrapDir < 0);
      syncField(wrapSteps, j.wrapSteps);
      syncField(wrapSweep, j.wrapSweep);
      const perN = (j.stepsPerRevX100 / 100) / j.numNails;
      wrapStepsNails.textContent =
        (+wrapSteps.value / perN).toFixed(2) + " nails" +
        (+wrapSteps.value === j.autoLead ? "" : "  (auto " + j.autoLead + ")");
      wrapSweepNails.textContent =
        (+wrapSweep.value / perN).toFixed(2) + " nails" +
        (+wrapSweep.value === j.autoSweep ? "" : "  (auto " + j.autoSweep + ")");
      syncField(wrapHoldInMs, j.wrapHoldInMs);
      syncField(wrapHoldSweepMs, j.wrapHoldSweepMs);
      syncField(servoSlewDeg, j.servoSlewDeg);
      syncField(servoSlewMs, j.servoSlewMs);
      syncField(calRehome, j.rehomeEvery);
      renderCal(j);
      renderWrapBreakdown(j);

      // Shown only when the saved position could not be trusted at boot.
      if (j.positionKnown === false) {
        idxResumeBanner.hidden = false;
        idxResumeBanner.innerHTML =
          "<b>Position not verified</b>Power was cut while the disc was moving, " +
          "so it may be parked between nails. Run Find Home, or line nail 0 up " +
          "by hand and press Set Home, before carrying on. Progress (step " +
          j.currentIndex + ") has been kept.";
      } else {
        idxResumeBanner.hidden = true;
      }
      renderEta(j);
      syncField(idxStepDelay, j.stepDelay);
      syncField(idxAutoMs, j.autoMs);
      if (document.activeElement !== idxReverseDir) idxReverseDir.checked = (j.dirSign < 0);
      idxAutoOn = j.autoRunning;
      idxAutoBtn.textContent = idxAutoOn ? "Stop Auto" : "Start Auto";
      idxFindHomeBtn.disabled = j.homing;
      idxSwitchText.textContent = "limit switch: " + (j.switchTriggered ? "TRIGGERED" : "open") +
        (j.homing ? "  (homing…)" : "") +
        (j.homeError ? "  — not found last time, check wiring" : "");
      if (document.activeElement !== feederAutoFeed) feederAutoFeed.checked = j.feederAutoFeed;
      syncField(feederRestAngle, j.feederRestAngle);
      syncField(feederFeedAngle, j.feederFeedAngle);
      restVal.textContent = feederRestAngle.value + "\u00b0";
      feedVal.textContent = feederFeedAngle.value + "\u00b0";
      servoLiveText.textContent = "Arm is at " + j.servoAngle + "\u00b0. Throw is " +
        Math.abs(feederFeedAngle.value - feederRestAngle.value) + "\u00b0.";
      syncField(feederPulseMs, j.feederPulseMs);
      syncField(feederSettleMs, j.feederSettleMs);
      syncField(feederRecoverMs, j.feederRecoverMs);
      updateCycleBreakdown();
    } catch (err) { /* machine momentarily busy mid-step; next poll will catch up */ }
  }

  async function idxAct(cmd, value) {
    let body = "cmd=" + cmd;
    if (value !== undefined) body += "&value=" + value;
    await fetch("/action", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" }, body });
    refreshIndexer();
  }

  document.getElementById("idxPrevBtn").addEventListener("click", () => idxAct("prev"));
  document.getElementById("idxNextBtn").addEventListener("click", () => idxAct("next"));
  document.getElementById("idxHomeBtn").addEventListener("click", () => idxAct("home"));
  idxAutoBtn.addEventListener("click", () => idxAct(idxAutoOn ? "stop" : "start"));
  idxFindHomeBtn.addEventListener("click", () => idxAct("findhome"));
  feederFeedBtn.addEventListener("click", () => idxAct("feed"));
  const idxGotoVal = document.getElementById("idxGotoVal");
  const idxGotoBtn = document.getElementById("idxGotoBtn");
  idxGotoBtn.addEventListener("click", () => idxAct("goto", idxGotoVal.value));
  idxGotoVal.addEventListener("keydown", (e) => { if (e.key === "Enter") idxGotoBtn.click(); });

  // Unlike "goto", this carries progress with it, so the wrap continues from
  // the chosen step rather than just parking the disc there.
  idxGotoStepBtn.addEventListener("click", () => {
    const v = parseInt(idxGotoStepVal.value, 10);
    if (Number.isFinite(v)) idxAct("gotostep", v);
  });
  idxGotoStepVal.addEventListener("keydown", (e) => { if (e.key === "Enter") idxGotoStepBtn.click(); });

  // ---- calibration ------------------------------------------------------
  let stepsPerNail = 1;

  // Jog moves whole nails where that is sensible, and single steps when the
  // nails are coarse enough that one nail would overshoot what you are trying
  // to line up.
  document.querySelectorAll("[data-jog]").forEach(b => {
    b.addEventListener("click", () => {
      const n = parseInt(b.dataset.jog, 10);
      idxAct("jog", Math.round(n * stepsPerNail));
    });
  });

  calSetBtn.addEventListener("click", () => {
    const v = parseInt(calNailVal.value, 10);
    if (Number.isFinite(v)) idxAct("setnail", v);
  });
  calMoveBtn.addEventListener("click", () => {
    const v = parseInt(calRevsVal.value, 10) || 10;
    idxAct("calmove", v);
  });
  calReportBtn.addEventListener("click", () => {
    const v = parseInt(calErrVal.value, 10);
    if (Number.isFinite(v)) idxAct("calreport", v);
  });

  function renderCal(j){
    stepsPerNail = (j.stepsPerRevX100 / 100) / j.numNails;
    calJogUnit.textContent =
      "One press = one nail (" + stepsPerNail.toFixed(1) + " steps, " +
      (360 / j.numNails).toFixed(2) + "\u00b0).";

    const offNails = j.nailOffsetSteps / stepsPerNail;
    calOffsetText.textContent = j.nailOffsetSteps === 0
      ? "No correction applied."
      : "Correction: " + j.nailOffsetSteps + " steps (" +
        offNails.toFixed(2) + " nails).";

    const now = j.stepsPerRevX100 / 100;
    const def = j.stepsPerRevDefX100 / 100;
    const drift = (now - def) / def * 100;
    calStepsText.className = "sync-box " + (Math.abs(drift) > 3 ? "approx" : "exact");
    calStepsText.innerHTML =
      "<b>" + now.toFixed(2) + "</b> steps per turn " +
      "<span class=\"dim\">(compiled default " + def.toFixed(2) + ", " +
      (drift >= 0 ? "+" : "") + drift.toFixed(2) + "%)</span><br>" +
      "<span class=\"dim\">One nail out after " +
      (Math.abs(drift) < 0.001 ? "\u221e" :
        Math.round(100 / Math.abs(drift) / j.numNails * 100) / 100 + " turns") +
      " of accumulated error at this figure.</span>";

    calRehomeText.textContent = !j.hasLimitSwitch
      ? "No limit switch fitted, so this cannot run."
      : (j.rehomeEvery > 0
        ? "Re-homes every " + j.rehomeEvery + " nails, keeping your place."
        : "Off. On a long run this is the only thing that clears missed steps.");
  }
  // One save for the whole Advanced panel -- motor, feed cycle and servo
  // angles go up together, so there is no half-applied state to reason about.
  async function saveAdvanced() {
    const body =
      "numNails=" + NailCount.get() +
      "&stepDelay=" + idxStepDelay.value +
      "&autoMs=" + idxAutoMs.value +
      "&dirSign=" + (idxReverseDir.checked ? -1 : 1) +
      "&feederRestAngle=" + feederRestAngle.value +
      "&feederFeedAngle=" + feederFeedAngle.value +
      "&feederPulseMs=" + feederPulseMs.value +
      "&feederSettleMs=" + feederSettleMs.value +
      "&feederRecoverMs=" + feederRecoverMs.value +
      "&feederAutoFeed=" + (feederAutoFeed.checked ? 1 : 0) +
      "&autoHomeOnBoot=" + (idxAutoHome.checked ? 1 : 0) +
      "&wrapMode=" + (wrapMode.checked ? 1 : 0) +
      "&wrapSteps=" + wrapSteps.value +
      "&wrapDir=" + (wrapDirFlip.checked ? -1 : 1) +
      "&wrapSweep=" + wrapSweep.value +
      "&wrapHoldInMs=" + wrapHoldInMs.value +
      "&wrapHoldSweepMs=" + wrapHoldSweepMs.value +
      "&servoSlewDeg=" + servoSlewDeg.value +
      "&servoSlewMs=" + servoSlewMs.value +
      "&rehomeEvery=" + calRehome.value;
    try {
      await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body,
      });
      clearDirty();
      showAdvMsg("Saved to the machine.", true);
    } catch (err) {
      showAdvMsg("Could not reach the machine.", false);
      return;
    }
    refreshIndexer();
  }
  advSaveBtn.addEventListener("click", saveAdvanced);

  advResetBtn.addEventListener("click", async () => {
    try {
      await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "reset=1",
      });
      clearDirty();
      showAdvMsg("Defaults restored.", true);
    } catch (err) {
      showAdvMsg("Could not reach the machine.", false);
      return;
    }
    refreshIndexer();
  });

  // The direction toggle is a switch, not a text field, so it saves at once.
  idxReverseDir.addEventListener("change", saveAdvanced);
  idxAutoHome.addEventListener("change", saveAdvanced);

  // ---- live servo angles -----------------------------------------------
  // The arm follows the slider while you drag. Throttled, because a range
  // input fires on every pixel and the ESP8266 does not need 60 requests a
  // second to move a servo 2 degrees at a time.
  let servoSendAt = 0, servoPending = null;
  function previewAngle(a) {
    servoPending = a;
    const now = Date.now();
    if (now - servoSendAt < 120) return;
    servoSendAt = now;
    const send = servoPending; servoPending = null;
    fetch("/action", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "cmd=servotest&value=" + send,
    }).catch(() => {});
  }
  feederRestAngle.addEventListener("input", () => {
    restVal.textContent = feederRestAngle.value + "\u00b0";
    previewAngle(feederRestAngle.value);
  });
  feederFeedAngle.addEventListener("input", () => {
    feedVal.textContent = feederFeedAngle.value + "\u00b0";
    previewAngle(feederFeedAngle.value);
  });
  // Dragging previews; letting go saves and parks the arm back at rest.
  for (const el of [feederRestAngle, feederFeedAngle]) {
    el.addEventListener("change", async () => {
      await saveAdvanced();
      previewAngle(feederRestAngle.value);
    });
  }
  servoGoRest.addEventListener("click", () => previewAngle(feederRestAngle.value));
  servoGoFeed.addEventListener("click", () => previewAngle(feederFeedAngle.value));
  servoSwing.addEventListener("click", () => idxAct("feed"));

  wrapRecalcBtn.addEventListener("click", async () => {
    await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "recalcWrap=1",
    });
    clearDirty();
    refreshIndexer();
  });
  wrapMode.addEventListener("change", saveAdvanced);
  wrapDirFlip.addEventListener("change", saveAdvanced);
  wrapTestBtn.addEventListener("click", () => idxAct("wraptest"));

  // Approaching a nail from each side in turn. Both should now make the same
  // little reversing move at the nail; before the fix one of them just carried
  // straight on and dropped the thread.
  async function testFrom(side) {
    const st = await (await fetch("/status")).json();
    if (!st.total) { wrapTestText.textContent = "Load a sequence first."; return; }
    const nail = st.nextNail;
    const n = st.numNails;
    const away = ((nail - side * 6) % n + n) % n;   // park six nails to one side
    wrapTestText.textContent = "Parking at nail " + away + "\u2026";
    await idxAct("goto", away);
    await new Promise(r => setTimeout(r, 1800));
    wrapTestText.textContent = "Wrapping nail " + nail + ", approaching from " +
      (side > 0 ? "+" : "\u2212") + ". Watch for the reversing move.";
    await idxAct("wraptest");
  }
  wrapTestCwBtn.addEventListener("click", () => testFrom(-1));
  wrapTestCcwBtn.addEventListener("click", () => testFrom(1));

  // Shows the loop the tube will actually trace, in steps and in nails, so an
  // overshoot that is too small to clear the neighbouring nails is obvious
  // before you run it rather than after a hundred dropped wraps.
  function renderWrapBreakdown(j){
    if (!j.wrapMode) {
      wrapBreakdown.className = "sync-box approx";
      wrapBreakdown.innerHTML =
        "<b>Wrapping off.</b> <span class=\"dim\">The servo pays thread out and " +
        "retracts along the same path, which encloses nothing. The thread will " +
        "not stay on a nail.</span>";
      return;
    }
    const perNail = (j.stepsPerRevX100 / 100) / j.numNails;
    const lead = j.wrapSteps > 0 ? j.wrapSteps : j.wrapAutoSteps;
    const sweep = j.wrapSweep > 0 ? j.wrapSweep : j.wrapAutoSweep;
    const travel = Math.abs(j.feederFeedAngle - j.feederRestAngle);
    const slewMs = Math.ceil(travel / Math.max(1, j.servoSlewDeg)) * j.servoSlewMs;
    const total = j.feederSettleMs + slewMs + j.wrapHoldInMs +
                  j.wrapHoldSweepMs + slewMs + j.feederRecoverMs;
    const overshootF = lead / perNail;
    const backF = (sweep - lead) / perNail;
    const risky = Math.abs(overshootF - Math.round(overshootF)) < 0.25 ||
                  Math.abs(backF - Math.round(backF)) < 0.25;

    wrapBreakdown.className = "sync-box " + (risky ? "approx" : "exact");
    wrapBreakdown.innerHTML =
      "Overshoots <b>" + overshootF.toFixed(2) + "</b> nails past the target, " +
      "sweeps back <b>" + (sweep / perNail).toFixed(2) + "</b>, lands on it<br>" +
      "<span class=\"dim\">handed off the direction of travel (" +
      (j.wrapApproachDir > 0 ? "+" : "\u2212") + " last time)" +
      (j.wrapDir < 0 ? ", globally flipped" : "") + "</span><br>" +
      "<span class=\"dim\">servo " + travel + "&deg; in " +
      (slewMs / 1000).toFixed(2) + " s each way &middot; cycle " +
      (total / 1000).toFixed(2) + " s plus disc travel and dwell.</span>" +
      (risky
        ? "<br><b>A crossing lands on a nail.</b> Adjust the offset or sweep so " +
          "both fall near .5 of a pitch."
        : "");
  }

  // Third view of the shared nail count.
  idxNumNails.addEventListener("input", () => NailCount.set(idxNumNails.value, "machine-settings"));
  NailCount.on((v, src) => {
    if (src === "machine-settings") return;
    idxNumNails.value = v;
    dirty.delete(idxNumNails.id);   // NailCount saves itself; nothing pending here
  });

  // Sends the disc to nail 0, then a quarter, half and three-quarters of the
  // way round. Watch the feeder: the nail arriving must be the one named.
  idxDirTestBtn.addEventListener("click", async () => {
    const n = parseInt(idxNumNails.value, 10) || 200;
    const stops = [0, Math.round(n / 4), Math.round(n / 2), Math.round(3 * n / 4), 0];
    idxDirTestBtn.disabled = true;
    for (const s of stops) {
      idxDirTestText.textContent = "at feeder should be: nail " + s;
      await idxAct("goto", s);
      await new Promise(r => setTimeout(r, 2500));
    }
    idxDirTestText.textContent = "done — if the nails did not match, flip the toggle above";
    idxDirTestBtn.disabled = false;
  });

  feederAutoFeed.addEventListener("change", saveAdvanced);

  setInterval(refreshIndexer, 1000);
  refreshIndexer();
  paintBackground();
})();
</script>
<script>
/* ---- Base template designer -------------------------------------------
   Draws the printable/laser-cuttable nail ring for the physical base.

   It reads the motor profile from /status rather than hardcoding one, so a
   template can never be printed for a step count the firmware isn't using.
   Nail 0 sits at three o'clock and numbering increases clockwise, matching
   pinPositions() in the generator above and the Python generator, so a nail
   index means the same physical nail everywhere in the toolchain.
   Adapted from StringArt-CircleBase-Design by Chanchal Sakarde.            */
(function(){
  "use strict";
  const PX_PER_MM = 96 / 25.4;
  const mm = v => v * PX_PER_MM;
  const esc = s => String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
  const $ = id => document.getElementById(id);

  const el = {
    count:$("baseCount"), saved:$("baseCountSaved"), radius:$("baseRadius"),
    font:$("baseFont"), margin:$("baseMargin"), cutMargin:$("baseCutMargin"),
    dots:$("baseDots"), centre:$("baseCentre"), cut:$("baseCut"),
    sync:$("baseSync"), suggest:$("baseSuggestRow"), preview:$("basePreview"),
    paper:$("basePaper"), save:$("baseSaveBtn"), open:$("baseOpenBtn"),
  };

  // Falls back to the 28BYJ-48 half-step default if the machine is unreachable
  // (e.g. page saved offline), and says so rather than pretending.
  let motor = { stepsPerRev:4096, internalSteps:32, halfStep:true, gearRatio:64, live:false };

  const A_SERIES = [["A6",105,148],["A5",148,210],["A4",210,297],["A3",297,420],
                    ["A2",420,594],["A1",594,841],["A0",841,1189]];
  const US_SERIES = [["US Letter",215.9,279.4],["US Legal",215.9,355.6],["Tabloid",279.4,431.8],
                     ["ANSI C",431.8,558.8],["ANSI D",558.8,863.6],["ANSI E",863.6,1117.6]];
  const fits = (list, side) => list.find(p => Math.min(p[1],p[2]) >= side - 0.05) || null;

  function params(){
    return {
      count: NailCount.get(),
      radiusMM: Math.max(10, +el.radius.value || 10),
      fontPX: Math.max(1, +el.font.value || 1),
      marginMM: Math.max(0, +el.margin.value || 0),
      cutMarginMM: Math.max(0, +el.cutMargin.value || 0),
    };
  }

  function geom(p, cutOn){
    const effMargin = cutOn ? Math.max(p.marginMM, p.cutMarginMM + 2) : p.marginMM;
    const widthMM = p.radiusMM * 2 + 2 * effMargin;
    const widthPX = mm(widthMM);
    return {
      widthMM, widthPX,
      cx: widthPX/2, cy: widthPX/2,
      rPX: mm(p.radiusMM),
      cutRadiusMM: p.radiusMM + p.cutMarginMM,
      cutRPX: mm(p.radiusMM + p.cutMarginMM),
      fontMinor: Math.max(1, p.fontPX - 3),
    };
  }

  // Nail 0 at angle 0 (three o'clock); angle grows clockwise because SVG y
  // points down. Same convention as the chord generator.
  function ringPoint(g, i, count){
    const a = 2 * Math.PI * i / count;
    return { x: g.cx + g.rPX * Math.cos(a), y: g.cy + g.rPX * Math.sin(a), deg: a * 180 / Math.PI };
  }

  function numbersGroup(p, g){
    let s = "";
    for (let i = 0; i < p.count; i++){
      const q = ringPoint(g, i, p.count);
      const major = (i % 5 === 0);
      const label = major ? String(i) : String(i % 10);
      const fs = major ? p.fontPX : g.fontMinor;
      s += '<text font-size="' + fs + 'px" transform="rotate(' + q.deg + ',' + q.x + ',' + q.y + ')">' +
           '<tspan x="' + q.x + '" y="' + q.y + '">-' + esc(label) + '</tspan></text>';
    }
    return '<g id="nail-numbers" font-family="Arial, Helvetica, sans-serif" text-anchor="start" ' +
           'dominant-baseline="middle" fill="#000">' + s + '</g>';
  }

  function dotsGroup(p, g){
    const r = mm(0.3);
    let s = "";
    for (let i = 0; i < p.count; i++){
      const q = ringPoint(g, i, p.count);
      s += '<circle cx="' + q.x + '" cy="' + q.y + '" r="' + r + '"></circle>';
    }
    return '<g id="nail-dots" fill="#ff0000" stroke="none">' + s + '</g>';
  }

  // Drill/pin reference for the exact centre of the board, sized in real mm
  // so it stays a sensible size whatever the ring radius is.
  function centreGroup(g){
    const arm = mm(5), dot = mm(0.5), sw = mm(0.25);
    return '<g id="centre-mark" stroke="#000" stroke-width="' + sw + '" fill="#000">' +
      '<line x1="' + (g.cx-arm) + '" y1="' + g.cy + '" x2="' + (g.cx+arm) + '" y2="' + g.cy + '"></line>' +
      '<line x1="' + g.cx + '" y1="' + (g.cy-arm) + '" x2="' + g.cx + '" y2="' + (g.cy+arm) + '"></line>' +
      '<circle cx="' + g.cx + '" cy="' + g.cy + '" r="' + dot + '" stroke="none"></circle></g>';
  }

  // Red stroke is the usual cut-line convention in hobby laser software.
  function cutGroup(g){
    return '<g id="cut-circle" fill="none" stroke="#ff0000" stroke-width="' + mm(0.5) + '">' +
      '<circle cx="' + g.cx + '" cy="' + g.cy + '" r="' + g.cutRPX + '"></circle></g>';
  }

  function svgMarkup(p, g, cutOn, forExport){
    const dims = forExport
      ? 'width="' + g.widthMM + 'mm" height="' + g.widthMM + 'mm"'
      : 'width="' + g.widthPX + '" height="' + g.widthPX + '"';
    let body = "";
    if (!forExport) {
      body += '<g fill="none" stroke="#dddddd" stroke-width="1"><circle cx="' + g.cx +
              '" cy="' + g.cy + '" r="' + g.rPX + '"></circle></g>';
    }
    if (cutOn) body += cutGroup(g);
    if (el.centre.checked) body += centreGroup(g);
    body += numbersGroup(p, g);
    if (el.dots.checked) body += dotsGroup(p, g);
    return '<svg xmlns="http://www.w3.org/2000/svg" ' + dims +
           ' viewBox="0 0 ' + g.widthPX + ' ' + g.widthPX + '">' + body + '</svg>';
  }

  function divisors(n, lo, hi){
    const out = [];
    for (let d = lo; d <= hi; d++) if (n % d === 0) out.push(d);
    return out;
  }

  function renderSync(p){
    const spr = motor.stepsPerRev;
    const stepDeg = 360 / spr;
    const perNail = spr / p.count;
    const exact = Math.abs(perNail - Math.round(perNail)) < 1e-9;
    const nailDeg = 360 / p.count;
    const pitchMM = 2 * Math.PI * p.radiusMM / p.count;
    // Worst case a nail can miss its true angle by half a step, since the
    // target step is rounded to the nearest whole step.
    const errDeg = exact ? 0 : 0.5 * stepDeg;
    const errMM = p.radiusMM * errDeg * Math.PI / 180;

    el.sync.className = "sync-box " + (exact ? "exact" : "approx");
    let html =
      '<b>' + motor.internalSteps + '</b> internal steps' +
      (motor.halfStep ? ' <span class="dim">x2 half-step</span>' : '') +
      ' <span class="dim">x</span> <b>' + motor.gearRatio.toFixed(motor.gearRatio % 1 ? 5 : 2) + ':1</b>' +
      ' <span class="dim">=</span> <b>' + (spr % 1 ? spr.toFixed(2) : spr) + '</b> steps/rev' +
      ' <span class="dim">(' + stepDeg.toFixed(4) + '&deg;/step)</span><br>' +
      '<b>' + (Math.round(perNail*1000)/1000) + '</b> steps per nail' +
      ' <span class="dim">&middot; ' + nailDeg.toFixed(3) + '&deg; apart &middot; ' +
      pitchMM.toFixed(2) + ' mm pitch</span><br>';
    if (exact) {
      html += '<span class="dim">Divides evenly &mdash; every nail lands exactly on a step.</span>';
    } else {
      html += '<span class="dim">Rounds to the nearest step: up to &plusmn;' + errDeg.toFixed(4) +
              '&deg; (' + errMM.toFixed(3) + ' mm at this radius).</span>';
    }
    if (perNail < 2) {
      html += '<br><b>Too many nails for this motor</b> &mdash; under 2 steps between ' +
              'neighbours, the indexer cannot separate them.';
    } else if (pitchMM < 3) {
      html += '<br><b>Nails only ' + pitchMM.toFixed(1) + ' mm apart</b> &mdash; ' +
              'increase the radius or drop the count.';
    }
    if (!motor.live) {
      html += '<br><span class="dim">Machine not reachable &mdash; showing compiled-in defaults.</span>';
    }
    el.sync.innerHTML = html;

    // Counts that divide the step grid evenly, filtered to ones you could
    // actually drill at this radius, nearest to the current value first.
    const cands = divisors(Math.round(spr), 24, 720)
      .filter(d => 2 * Math.PI * p.radiusMM / d >= 3)
      .sort((a,b) => Math.abs(a - p.count) - Math.abs(b - p.count))
      .slice(0, 3);
    el.suggest.innerHTML = "";
    if (!exact && cands.length) {
      cands.forEach(d => {
        const b = document.createElement("button");
        b.className = "ghost";
        b.textContent = d;
        b.title = "Exact: " + (spr / d) + " steps per nail";
        b.addEventListener("click", () => NailCount.set(d, "base"));
        el.suggest.appendChild(b);
      });
    }
  }

  function render(){
    const p = params();
    const cutOn = el.cut.checked;
    const g = geom(p, cutOn);
    el.saved.textContent = NailCount.isUnsaved() ? "saving…" : "on machine";
    el.preview.innerHTML = svgMarkup(p, g, cutOn, false);
    renderSync(p);
    const a = fits(A_SERIES, g.widthMM), u = fits(US_SERIES, g.widthMM);
    el.paper.textContent = (a || u)
      ? "Prints at 100% on " + [a && a[0], u && u[0]].filter(Boolean).join(" or ") +
        " (" + g.widthMM.toFixed(0) + " mm square) - turn off 'fit to page'."
      : "Bigger than A0 / ANSI E (" + g.widthMM.toFixed(0) + " mm square) - tile it or reduce the radius.";
  }

  function exportSvg(){
    const p = params();
    return { text: svgMarkup(p, geom(p, el.cut.checked), el.cut.checked, true), count: p.count };
  }

  el.save.addEventListener("click", () => {
    const out = exportSvg();
    const url = URL.createObjectURL(new Blob([out.text], { type:"image/svg+xml" }));
    const a = document.createElement("a");
    a.href = url;
    a.download = "string_art_base_" + out.count + "_nails.svg";
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 4000);
  });

  el.open.addEventListener("click", () => {
    const url = URL.createObjectURL(new Blob([exportSvg().text], { type:"image/svg+xml" }));
    window.open(url, "_blank");
    setTimeout(() => URL.revokeObjectURL(url), 30000);
  });

  el.count.addEventListener("input", () => NailCount.set(el.count.value, "base"));
  el.count.addEventListener("change", () => NailCount.set(el.count.value, "base"));
  NailCount.on((v, src) => {
    if (src !== "base") el.count.value = v;
    render();
  });

  [el.radius, el.font, el.margin, el.cutMargin].forEach(i => {
    i.addEventListener("input", render);
    i.addEventListener("change", render);
  });
  [el.dots, el.centre, el.cut].forEach(i => i.addEventListener("change", render));

  // Pull the motor profile and current nail count once at load. The profile is
  // compile-time constant, so there is nothing to re-poll.
  (async function init(){
    try {
      const j = await (await fetch("/status")).json();
      if (typeof j.stepsPerRevX100 === "number") {
        motor = {
          stepsPerRev: j.stepsPerRevX100 / 100,
          internalSteps: j.internalSteps,
          halfStep: !!j.halfStep,
          gearRatio: j.gearRatioX100000 / 100000,
          live: true,
        };
      }
      if (j.numNails) NailCount.adopt(j.numNails);
    } catch (err) { /* offline: keep the defaults and flag it in the readout */ }
    render();
  })();
})();
</script>
</body>
</html>

)STRINGARTPAGE";
