// Part 3 of the games' quality range gate (tests/quality-range.mjs). The party pack draws a quarter of a millisecond on
// WebGPU, where the timers' steps move one preset by about a quarter run to run: the order between presets takes a 35%
// slack in this part (the shares of High are unchanged).
import { runPart } from './quality-range.mjs';
process.exit(runPart('3', ['--slack', '0.35']));
