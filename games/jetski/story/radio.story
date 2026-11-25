# jetski's race radio: the crew chief of the rider you pick speaks only on news, and the other riders call out a pass.
# Compiled by the engine's tools/story-compile.mjs to assets/radio.cwstory; voiced by tools/speech-bake.mjs (Kokoro-82M).
# The game asks for these pools (src/radio.c); every pool has a cooldown and no line follows itself.

cast cruz_crew "Cruz's crew" voice=bm_george speed=1.0 color=#ff8a6a
cast marlin_crew "Marlin's crew" voice=am_michael speed=1.05 color=#7ab8ff
cast kestrel_crew "Kestrel's crew" voice=bf_emma speed=1.08 color=#ffd060
cast tide_crew "Tide's crew" voice=am_adam speed=0.95 color=#7be0a0
cast juno_crew "Juno's crew" voice=af_bella speed=1.05 color=#d79bff
cast reef_crew "Reef's crew" voice=bm_lewis speed=1.0 color=#e8e8e8
cast cruz "Cruz" voice=am_michael speed=1.1 color=#ff8a6a
cast marlin "Marlin" voice=am_adam speed=1.1 color=#7ab8ff
cast kestrel "Kestrel" voice=af_heart speed=1.15 color=#ffd060
cast tide "Tide" voice=bm_george speed=1.0 color=#7be0a0
cast juno "Juno" voice=af_bella speed=1.12 color=#d79bff
cast reef "Reef" voice=bf_isabella speed=1.05 color=#e8e8e8

# the crew: news only
pool cruz_crew.waves cooldown=20
  line cruz_crew "Big set coming through. Keep the nose up and ride it."
  line cruz_crew "Waves on the way. Meet them square."
end
pool cruz_crew.lastlap cooldown=20
  line cruz_crew "Last lap. Clean marks, no heroics."
  line cruz_crew "Final lap. Bring it home."
end
pool cruz_crew.closing cooldown=25
  line cruz_crew "Somebody's on your tail. Hold your line."
  line cruz_crew "Company behind you. Don't give them the inside."
end
pool cruz_crew.lead cooldown=30
  line cruz_crew "You're out front. Keep it smooth."
end
pool cruz_crew.route cooldown=30
  line cruz_crew "The short way is open today. Take it if you can."
end

pool marlin_crew.waves cooldown=20
  line marlin_crew "Swell's building out there. Use it, don't fight it."
  line marlin_crew "Here come the big ones. Speed over the top."
end
pool marlin_crew.lastlap cooldown=20
  line marlin_crew "Last lap. Open it up on the straights."
  line marlin_crew "One to go. Flat out."
end
pool marlin_crew.closing cooldown=25
  line marlin_crew "They're coming. You've got the top end, use it."
  line marlin_crew "Rider closing. Out-run them."
end
pool marlin_crew.lead cooldown=30
  line marlin_crew "Front of the pack. Now stretch it."
end
pool marlin_crew.route cooldown=30
  line marlin_crew "The shortcut's open. Worth a look."
end

pool kestrel_crew.waves cooldown=20
  line kestrel_crew "Waves incoming! Quick hands now."
  line kestrel_crew "Big water ahead. Light on the bars."
end
pool kestrel_crew.lastlap cooldown=20
  line kestrel_crew "Last lap! Sharp through the marks."
  line kestrel_crew "Final lap, Kestrel. Tight lines."
end
pool kestrel_crew.closing cooldown=25
  line kestrel_crew "Someone's closing. Beat them out of the turns."
  line kestrel_crew "Pressure behind. Stay quick."
end
pool kestrel_crew.lead cooldown=30
  line kestrel_crew "You've got the lead. Keep darting."
end
pool kestrel_crew.route cooldown=30
  line kestrel_crew "Short way's open. Go on, cut it."
end

pool tide_crew.waves cooldown=20
  line tide_crew "Heavy water coming. Plant it and push through."
  line tide_crew "Big waves. You're the heaviest out there, lean on it."
end
pool tide_crew.lastlap cooldown=20
  line tide_crew "Last lap. Nobody moves you now."
  line tide_crew "Final lap. Hold your ground."
end
pool tide_crew.closing cooldown=25
  line tide_crew "Rider on your hip. Let them bounce off."
  line tide_crew "They're closing. Shut the door."
end
pool tide_crew.lead cooldown=30
  line tide_crew "Out front. Make yourself wide."
end
pool tide_crew.route cooldown=30
  line tide_crew "Shortcut's open. Your call."
end

pool juno_crew.waves cooldown=20
  line juno_crew "Waves! That's air waiting for you."
  line juno_crew "Big set coming. Time for something fancy."
end
pool juno_crew.lastlap cooldown=20
  line juno_crew "Last lap. Make it pretty."
  line juno_crew "Final lap. Every ramp, Juno."
end
pool juno_crew.closing cooldown=25
  line juno_crew "Somebody's catching up. Find a ramp."
  line juno_crew "Rider behind you. Fly away from them."
end
pool juno_crew.lead cooldown=30
  line juno_crew "You're leading. Show off a little."
end
pool juno_crew.route cooldown=30
  line juno_crew "The short way's open. Lovely."
end

pool reef_crew.waves cooldown=20
  line reef_crew "Waves ahead. Carve through, don't jump."
  line reef_crew "Big water. Keep it tidy."
end
pool reef_crew.lastlap cooldown=20
  line reef_crew "Last lap. Precise, like every lap."
  line reef_crew "Final lap. Nail every mark."
end
pool reef_crew.closing cooldown=25
  line reef_crew "Rider closing. Take the tight line, they can't."
  line reef_crew "Someone behind. Turn inside them."
end
pool reef_crew.lead cooldown=30
  line reef_crew "Front of the race. Steady hands."
end
pool reef_crew.route cooldown=30
  line reef_crew "Shortcut's open. Neat way through."
end

# the riders: a call when they pass you, or you pass them
pool cruz.passes cooldown=15
  line cruz "Coming through!"
  line cruz "See you at the line."
end
pool cruz.passed cooldown=15
  line cruz "Not for long."
end
pool marlin.passes cooldown=15
  line marlin "Too slow, friend."
  line marlin "Enjoy the spray."
end
pool marlin.passed cooldown=15
  line marlin "I'll have that back on the straight."
end
pool kestrel.passes cooldown=15
  line kestrel "Excuse me!"
  line kestrel "Inside line, thanks."
end
pool kestrel.passed cooldown=15
  line kestrel "Oh, cheeky."
end
pool tide.passes cooldown=15
  line tide "Out of my way."
  line tide "Move over."
end
pool tide.passed cooldown=15
  line tide "Bold. We'll see."
end
pool juno.passes cooldown=15
  line juno "Watch this!"
  line juno "Bye for now!"
end
pool juno.passed cooldown=15
  line juno "Nice move. Mine's better."
end
pool reef.passes cooldown=15
  line reef "Tidy, wasn't it?"
  line reef "Clean pass."
end
pool reef.passed cooldown=15
  line reef "Fair enough."
end
