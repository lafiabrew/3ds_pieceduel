#!/usr/bin/env node
/**
 * Automated Cross-Language Differential Compatibility Test Suite
 * Compares the C11 3DS engine (`build/host/pd_cli`) against the canonical
 * TypeScript `@pieceduel/engine` in `/home/lafiamafia/sandbox/pieceduel/packages/engine`
 * across PRNG, FNV-1a dateSeed, full AI playthroughs (standard, misere, advanced),
 * Daily Puzzles, Win-in-Two, Safe-Give, Threat-Drill, and Move Coaching (`reviewMove`).
 */

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const PD_CLI = path.join(ROOT, "build", "host", "pd_cli");
const ENGINE_INDEX = path.resolve("/home/lafiamafia/sandbox/pieceduel/packages/engine/src/index.ts");

function runCli(args) {
  const out = execFileSync(PD_CLI, args.map(String), { encoding: "utf8" });
  return JSON.parse(out);
}

async function loadTsEngine() {
  return await import(pathToFileURL(ENGINE_INDEX).href);
}

async function main() {
  const ts = await loadTsEngine();
  let totalChecks = 0;

  // 1. Mulberry32 PRNG bit-exact check
  console.log("[1/8] Checking Mulberry32 PRNG bit-exactness...");
  for (const seed of [0, 1, 42, 12345, 0x9e3779b9, 0xffffffff]) {
    const cVals = runCli(["rng", seed, 50]);
    const rng = ts.createRng(seed);
    for (let i = 0; i < 50; i += 1) {
      const tsVal = rng();
      assert.ok(
        Math.abs(cVals[i] - tsVal) < 1e-15,
        `PRNG mismatch at seed=${seed} idx=${i}: C=${cVals[i]} TS=${tsVal}`,
      );
      totalChecks += 1;
    }
  }

  // 2. FNV-1a dateSeed bit-exact check
  console.log("[2/8] Checking FNV-1a dateSeed parity...");
  for (let day = 1; day <= 30; day += 1) {
    const iso = `2026-11-${String(day).padStart(2, "0")}`;
    const cRes = runCli(["dateseed", iso]);
    const tsSeed = ts.dateSeed(iso);
    assert.equal(cRes.seed >>> 0, tsSeed >>> 0, `dateSeed mismatch for ${iso}`);
    totalChecks += 1;
  }

  // 3. Full AI playthroughs across variants & difficulties
  console.log("[3/8] Checking full AI playthrough parity across variants & levels...");
  const variants = ["standard", "misere", "advanced"];
  const diffs = ["easy", "medium", "hard"];
  for (const variant of variants) {
    for (const d0 of diffs) {
      for (const d1 of diffs) {
        for (const seed of [7, 99, 2026]) {
          const cGame = runCli(["play", seed, variant, d0, d1]);
          const rng = ts.createRng(seed);
          let st = ts.createGame(0, variant);
          const tsMoves = [];
          while (st.status === "playing") {
            const diff = st.current === 0 ? d0 : d1;
            const mv = ts.chooseMove(st, diff, rng);
            tsMoves.push({
              kind: mv.kind,
              cell: mv.kind === "place" ? mv.cell : null,
              give: mv.give,
            });
            st = ts.applyMove(st, mv);
          }
          assert.equal(cGame.status, st.status, `status mismatch (${variant} ${d0}v${d1} seed=${seed})`);
          assert.equal(cGame.winner, st.winner, `winner mismatch (${variant} ${d0}v${d1} seed=${seed})`);
          assert.equal(cGame.moveCount, st.moveCount, `moveCount mismatch (${variant} ${d0}v${d1} seed=${seed})`);
          assert.deepEqual(cGame.board, st.board, `board mismatch (${variant} ${d0}v${d1} seed=${seed})`);
          assert.deepEqual(cGame.moves, tsMoves, `moves mismatch (${variant} ${d0}v${d1} seed=${seed})`);
          totalChecks += 1;
        }
      }
    }
  }

  // 4. Daily Puzzle generator parity
  console.log("[4/8] Checking generateDailyPuzzle parity...");
  for (const variant of ["standard", "advanced"]) {
    for (let s = 1; s <= 25; s += 1) {
      const seed = (s * 2654435761) >>> 0;
      const cPuz = runCli(["daily", seed, variant]);
      const tsPuz = ts.generateDailyPuzzle(seed, variant);
      assert.deepEqual(cPuz.board, tsPuz.board, `daily board mismatch seed=${seed} variant=${variant}`);
      assert.equal(cPuz.pieceInHand, tsPuz.pieceInHand, `daily pieceInHand mismatch seed=${seed}`);
      assert.deepEqual(cPuz.solution, tsPuz.solution, `daily solution mismatch seed=${seed}`);
      assert.equal(cPuz.pieces, tsPuz.pieces, `daily pieces mismatch seed=${seed}`);
      totalChecks += 1;
    }
  }

  // 5. Win-in-Two puzzle generator & solver parity
  console.log("[5/8] Checking generateWinInTwo parity...");
  for (const variant of ["standard", "advanced"]) {
    for (let s = 1; s <= 12; s += 1) {
      const seed = (s * 1013904223) >>> 0;
      const cW2 = runCli(["win2", seed, variant]);
      const tsW2 = ts.generateWinInTwo(seed, variant);
      assert.deepEqual(cW2.board, tsW2.board, `win2 board mismatch seed=${seed} variant=${variant}`);
      assert.equal(cW2.pieceInHand, tsW2.pieceInHand, `win2 pieceInHand mismatch seed=${seed}`);
      assert.deepEqual(cW2.available, tsW2.available, `win2 available mismatch seed=${seed}`);
      assert.deepEqual(cW2.firstMoves, tsW2.firstMoves, `win2 firstMoves mismatch seed=${seed}`);
      assert.equal(cW2.pieces, tsW2.pieces, `win2 pieces mismatch seed=${seed}`);
      totalChecks += 1;
    }
  }

  // 6. Safe-Give puzzle generator parity
  console.log("[6/8] Checking generateSafeGive parity...");
  for (const variant of ["standard", "advanced"]) {
    for (let s = 1; s <= 20; s += 1) {
      const seed = (s * 1664525) >>> 0;
      const cSg = runCli(["safegive", seed, variant]);
      const tsSg = ts.generateSafeGive(seed, variant);
      assert.deepEqual(cSg.board, tsSg.board, `safegive board mismatch seed=${seed}`);
      assert.deepEqual(cSg.candidates, tsSg.candidates, `safegive candidates mismatch seed=${seed}`);
      assert.deepEqual(cSg.safe, tsSg.safe, `safegive safe mismatch seed=${seed}`);
      assert.equal(cSg.pieces, tsSg.pieces, `safegive pieces mismatch seed=${seed}`);
      assert.equal(cSg.threats, tsSg.threats, `safegive threats mismatch seed=${seed}`);
      totalChecks += 1;
    }
  }

  // 7. Threat-Drill (Hot Squares) generator parity
  console.log("[7/8] Checking generateThreatDrill parity...");
  for (const variant of ["standard", "advanced"]) {
    for (let s = 1; s <= 20; s += 1) {
      const seed = (s * 22695477) >>> 0;
      const cTd = runCli(["threatdrill", seed, variant]);
      const tsTd = ts.generateThreatDrill(seed, variant);
      assert.deepEqual(cTd.board, tsTd.board, `threatdrill board mismatch seed=${seed}`);
      assert.deepEqual(cTd.hot, tsTd.hot, `threatdrill hot mismatch seed=${seed}`);
      assert.equal(cTd.threats, tsTd.threats, `threatdrill threats mismatch seed=${seed}`);
      assert.equal(cTd.pieces, tsTd.pieces, `threatdrill pieces mismatch seed=${seed}`);
      totalChecks += 1;
    }
  }

  // 8. Post-game Move Coaching (`reviewMove`) parity
  console.log("[8/8] Checking reviewMove coaching parity...");
  for (let s = 1; s <= 15; s += 1) {
    const seed = (s * 314159265) >>> 0;
    const cReviews = runCli(["review", seed, "standard"]);
    const rng = ts.createRng(seed);
    let st = ts.createGame(0, "standard");
    const tsReviews = [];
    let ply = 0;
    while (st.status === "playing") {
      const mv = ts.chooseMove(st, "easy", rng);
      const played = {
        cell: mv.kind === "place" ? mv.cell : null,
        give: mv.give,
      };
      const rev = ts.reviewMove(st, played);
      tsReviews.push({
        ply,
        missedWinCell: rev.missedWinCell,
        unsafeGive: rev.unsafeGive,
        saferGive: rev.saferGive,
        forcedLoss: rev.forcedLoss,
      });
      st = ts.applyMove(st, mv);
      ply += 1;
    }
    assert.deepEqual(cReviews, tsReviews, `reviewMove mismatch for seed=${seed}`);
    totalChecks += 1;
  }

  console.log(`PASS: All ${totalChecks} cross-language differential checks matched bit-for-bit!`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
