# Brawler

One to four players fight through four street stages on one shared scrolling screen. Walk along the street
and up and down its width, fight a crowd that surrounds you, break crates and barrels for food and weapons,
beat a mini boss and a boss at the end of every stage.

## Controls (pad, or keyboard for player 1)

| Action | Pad | Keyboard |
|---|---|---|
| Move | stick or d-pad | WASD or arrows |
| Attack (three hit combo) | X | J or Z |
| Heavy kick | Y | K or X |
| Jump (attack in the air: flying kick) | A | Space |
| Grab | right bumper | L or C |
| Spin move (costs a little health) | left bumper | I or V |

Grab a body in front of you, then Attack to knee it. Three knees throw it, or hold the stick away from it and
press Grab or Kick to throw it over your shoulder. Thrown bodies knock down whoever they hit.

Walk over food, coins and weapons to pick them up; you can reach them from a step to either side.

## Who you fight

- Thugs and punks swing and kick; runners charge from a distance; knifers stab.
- Throwers keep their distance and throw bottles along your lane: step up or down the street, or jump, to dodge.
- Guards hold a riot shield: punches from the front bounce off (three of them break the guard). Kick, jump kick,
  throw, use a weapon or the spin move, or hit them from behind.
- Bruisers are heavy and do not flinch.
- Rednecks (a red cap that says "Dumb", a grimy tee) wind up a huge haymaker: when he turns his whole body away
  and a red ring shows at his feet, step out of his lane. It knocks you down if it lands, and he stumbles after it, so
  punish the miss. Now and then he lowers a shoulder and barges along the lane from a few steps away.
- No two of a kind look alike: every thug, punk, runner, knifer, bruiser, thrower and redneck walks on with his own skin
  tone, hair and clothes (the riot guards keep their uniform).
- Each stage ends with a boss with three phases. At two thirds and one third of its health it stops, roars, calls
  help and comes back with a new move. Heavy blows (slams, spins, charges) show a red ring on the street while they
  wind up: get out of it.
- The bosses, stage by stage: the Foreman (the alley), Arthur (the docks: a greaser who jabs from a boxer's guard,
  snaps his jacket open into a backhand, charges shoulder first, stops to comb his hair or give you a thumbs up, and
  from his second phase walks over to the jukebox on the pavement: when he raises his fist and its lights flicker, get
  ready to jump. He thumps it, his song starts, he gets quicker for a few seconds, and a shockwave runs out along the
  street from it: anyone on the ground when the ring reaches them is knocked down. Jump over it), the Matron (the arcade,
  where the Ringmaster also runs the floor as a mini boss halfway through) and the Colossus (the roof).

Hard blows draw a little blood, and bodies that hit the street leave a stain (they fade after a while).

Anyone knocked down falls loose: the body tumbles, hits the street, the kerb and the crates, and gets up from where it
lies (face up or face down). The fall is only for show: how long a body stays down is the same every time.

Enemies never hit you from outside the picture or before they have walked in.

## Run

`brawler_auto` is the same game with bots playing, for tests and screenshots. Its debug commands: `stage N`,
`wave N` (4 is the boss), `players N`, `spawn <kind> [dx dz]` (15 is the redneck), `item <kind> [dx dz]`, `cam`,
`freeze <0|1> [turn]` (turn: how far a frozen body turns from the camera, 1.57 side on), `knockdown`, `blood <dx> <dz> <kind>`.
Environment for native runs: BRAWLER_STAGE, BRAWLER_PLAYERS, BRAWLER_CROWD, BRAWLER_TURBO (ticks per frame), BRAWLER_KNOCKALL (everyone down every N ticks),
BRAWLER_SOAK (a lost stage is retried), BRAWLER_SEED (another match from the same rules), BRAWLER_LOG (status lines), BRAWLER_BOTDMG (bot damage factor, 3 by default).

`node games/brawler/tools/soak.mjs --exe <brawler_auto> --pak <brawler_auto.cwpak> --players 4 --minutes 30 --all-stages`
runs a soak on software GL and checks it (crash, NaN, token leak, unfair hits, stalls, memory, every boss).
`node games/brawler/tools/boss-ttk.mjs --exe <brawler_auto> --pak <brawler_auto.cwpak> --players 1,2,4 --seeds 6` times
every boss fight (seconds shown and blows landed) with the bots at a fighter's real damage.

## What it is for

It exists to test the engine: skinned characters with a shared clip library, the navigation crowd (enemies steer
and avoid each other; an attack-token pool decides who may hit), GPU particles for hits, dust and debris, a clustered
light pool for flashes, and the game shell. What the game found missing went back into the engine as generic features.
