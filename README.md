# Langevin

A clock bender for the [ER-301 Sound Computer](https://www.orthogonaldevices.com/er-301), after the ADDAC406 Clock Langevin. It takes a clock and makes two new ones, **A** and **B**, whose tempo glides away from the clock and back over a cycle you set. No two intervals inside a cycle are the same, yet every cycle starts back on a beat of the clock.

Unit title: **Langevin** · mnemonic **Lv** · current version **0.1.0**

Named for Paul Langevin, who posed the *twin paradox*: two clocks that run at different rates while apart, then meet again. Here A and B drift away from the clock and land back on its beat at the end of every cycle.

## One cycle

Each bender's cycle is counted in beats of the incoming clock:

1. **Going out:** the tempo glides from the clock's tempo to the clock × **mult**. It speeds up above 1 and slows down below 1, shaped by the **left** curve.
2. **Coming back:** it glides back to the clock's tempo, shaped by the **right** curve.
3. **Pause:** for **pause** beats it either **rests** (silent) or **holds** the tempo the curve ended on, chosen in the menu.

**beats** sets how long steps 1 and 2 take together. **bal** sets where the turn happens: 0 is coming-back only, 1 is going-out only, 0.5 is a symmetrical rise and fall. The curves go from − (slow start) through 0 (straight) to + (fast start).

A cycle **loops**, or with **one-shot** in the menu it runs once and waits for **reset**. **reset** restarts both benders on the next clock beat. When the clock stops, Langevin stops, and it starts a fresh cycle when the clock comes back.

## The taps

Langevin doesn't change the channel's sound: its own output is the channel's audio, passed through untouched, so you can put it anywhere in a channel. Its four taps are local sources:

| Tap | What it is |
|---|---|
| **A**, **B** | The bent clocks (5 ms triggers). |
| **A rate**, **B rate** | A CV that follows each tempo curve, on the ER-301's 1 V/oct scale: 0 at the clock's tempo, +0.1 at double. Patch it into Noether's **V/Oct** and the loop's speed bends along with the clock. |

To use one: on any control in the same channel, open its source chooser, go to **Locals**, highlight **Langevin**, press **M6** to step through the taps, then press **Enter**. This needs the stolmine firmware, 9.2 or later.

## Display

The tempo curves of both benders, A bright and B faint, each with a dot at the beat it's on, plus a lamp for each trigger. The sub display shows the clock's tempo and each bender's current tempo and beat.

## Building

`make dist` cross-compiles in Docker and packages `build/am335x/langevin-<version>.pkg`. `make hosttest` runs the host test suite. See `ER301-UNIT-STANDARD.md` for the rules every unit here follows.

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
