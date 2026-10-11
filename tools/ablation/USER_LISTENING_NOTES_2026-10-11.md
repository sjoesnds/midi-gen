# User listening feedback — 2026-10-11

Source: the user's completed `ratings-template.csv` for the original 10-pair pilot. Preserve these as qualitative product feedback. Do not treat the experiment as statistically sound evidence for score-weight tuning: pairs were drawn from different MAGIC variation slots (including slots 8 and 6), rather than consistently comparing the first-ranked result.

## What the user reported

- Overall scores were modest: both A and B averaged 3/5 across this tiny set. Most loops were scored 3/5; the best pair, P006, had both sides at 4/5.
- P001: user said the melodies were "not very good"; A (the normal baseline) scored 3 and B (pleasantness score ablated) scored 2.
- P002: user preferred B (normal baseline), 4 vs 2; note: "2nd melody is okay". This was variation slot 8, so it is not clean evidence about the first MAGIC pick.
- P003: user preferred A (normal baseline) 3 vs 2 over the motif-memory ablation.
- P004 and P009 tested groove-score ablation in opposite A/B order. The user slightly preferred A in both, with both pairs scored 3/5 on each side; P009 note says the second half of A sounded good. This is ambiguous for score-weight tuning but points toward phrase-level payoff and second-half development.
- P005: user said it sounded similar to P004. Treat this as a meaningful warning about insufficiently distinct ideas/variations.
- P006: both melodies scored 4/5; user said each was good in its own way. This is the clearest positive quality reference in this batch.
- P007: user said both melodies sounded worse than P006.
- P008 and P010 were ties; do not infer that the relevant score functions are useless from these ties.

## Development decisions

1. Prioritise **consistency of musical quality**: avoid banks dominated by merely acceptable 3/5 ideas, and improve the lower tail without flattening creative character.
2. Prioritise **distinct musical identities** between nearby MAGIC results/variations. P004/P005 similarity is direct feedback to address, not a reason to bolt on another abstract scoring layer.
3. Inspect **phrase development and payoff in the second half**. Preserve strong local moments and make the whole loop feel intentional, rather than improving only note-level statistics.
4. Keep the deterministic seed, tonal safety and strict rhythmic-grid checks as regression guards.
5. **Do not tune production weights from these 10 comparisons.** Do not ask the user to perform more spreadsheet calibration. The next quality change should be code-led, isolated, reviewed through existing headless tests, and judged by the user in normal FL Studio use when a real improvement is ready.

No production algorithm or weights are changed by this note.
