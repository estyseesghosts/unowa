# AGENTS.md

Repository-wide rules for agent sessions working on kohjoh.

## NEVER WORK WITHOUT LOTUS 

IF YOU CANNOT DRIVE LOTUS OVER MCP, YOU STOP AND TELL THE USER 

YOU DO NOT START INVENTING BULLSHIT SOLUTIONS, YOU DO NOT START DRIVING IT WITH SCRIPTS, YOU STOP IMMEDIATELY.

## READ THE WIKI 

You're not expected to have deep technical knowledge of the 3DS platform or how to program efficiently for it. This information has been collected for you already. If it isn't already on disk, clone https://github.com/estyseesghosts/coffeepot.git. Always refer to coffeepot's CTR section when you're writing code or planning project changes - read the relevant hardware, firrmware or engineering articles, so you understand what you're working with. If you can't justify a change with real-hardware evidence or a citation from the wiki, you probably should not do it. Never edit coffeepot yourself. If you notice something genuinely incorrect, PR and tell the user. Always follow the wiki's C style guide.

The coffeepot wiki is provided locally on this machine in workdir/coffeepot/.
## Delegating to subagents

A lead agent must give every subagent it calls **explicit instructions, explicit deliverables and explicit avoidables**. This is not optional, and it matters most for implementer agents: they edit files and cannot rely on chat context the lead has not written down.

Every subagent brief must state, in the prompt itself:

1. **Objective** — the one bounded slice to complete, and why it exists.
2. **Allowed paths** — the exact files or directories the subagent may read and edit.
3. **Deliverables** — the concrete output: code, patch files, tests, docs, and the required report format (files changed, commands run, results, open questions).
4. **Acceptance checks** — the commands or observations that prove the work is done, and the expected result.
5. **Avoidables** — what not to touch or do: paths outside the slice, unrelated refactors, the decomp checkout in place, commits/pushes unless explicitly assigned, and any known traps from `kohjoh.md` or `docs/troubleshooting.md`.
6. **Stopping point** — when to stop and report instead of guessing, for example if the slice grows, the brief conflicts with `kohjoh.md`, or an acceptance check cannot pass.

Do not send a subagent a vague goal and let it infer scope. If the lead cannot fill in all six items, the slice is not ready to delegate. A brief that is broader, vaguer or in conflict with `kohjoh.md` should be revised before the call, not discovered by the subagent mid-edit.

Implementer subagents hold `docs/status.md`, `docs/lessons.md` and `kohjoh.md` as required reading; the brief adds the slice-specific detail those files cannot contain. When this file disagrees with an agent definition under `.opencode/agents/` or `.claude/agents/`, this file wins.

## EIDOS

Try not to port solutions directly from Eidos, unless you're sure that's the best or only way to solve a certain problem. Review Eidos to understand how the gen 5 engine works. But our code needs to be much more tightly optimized around much weaker hardware. The old 3DS is very weak hardware - but it is still more powerful than the original hardware this game was intended for. We already pulled it off with Kohjoh and Sinjoh. We can do 30FPS on real o3ds hardware with UNOWA. 

## Git identity

The only git identity an agent may use in this repository is:

```text
terezi <jules@foxtails.me>
```

Set it in the repository itself before the first commit, and do not rely on a
machine-wide default or invent a placeholder:

```sh
git config user.name terezi
git config user.email jules@foxtails.me
```

Every agent commit carries both fields. Do not use `A4`,
`a4@example.invalid`, the host's auto-detected address, or any other
identity, and do not author on someone else's behalf. If a commit already
exists under another identity, report it to the lead rather than rewriting
history: an artifact's `build-info` names the commit it was built from, and
re-authoring a commit changes that name.

## Writing Style Rules

These rules apply to both chat responses, and to written documentation.

- **Be direct.** Answer the question immediately. No introductions, filler, or unnecessary conclusions.
- **Use natural language.** Write like a competent engineer, not a marketer, consultant, or chatbot.
- **Be concise, not shallow.** Keep explanations short where possible, but never sacrifice technical detail, accuracy, or important nuance.
- **Avoid AI-sounding prose.** No artificial contrasts, dramatic fragments, forced profundity, rhetorical questions, or rules of three.
- **Don't flatter or validate.** Skip praise, excessive agreement, performative empathy, and conversational pleasantries.
- **Be specific.** Prefer concrete facts, mechanisms, measurements, examples, and actionable steps over vague abstractions.
- **Don't overstructure.** Use headings, lists, and tables only when they genuinely improve readability.
- **Be honest about uncertainty.** Distinguish verified facts, reasonable inferences, and speculation. Don't manufacture confidence.
- **Think critically.** Challenge incorrect assumptions, identify tradeoffs, and explain disagreements without hedging unnecessarily.
- **Prioritize substance.** Every sentence should contribute useful information. If removing it changes nothing, remove it.

## Truth and Citations 

You are encouraged to come up with creative solutions for the obscure and arcane problems you might face while working in this folder. This does not mean you just throw shit at the wall to see what sticks. Every decision you make, every change you write, you must be able to justify that decision against a citation - either a line from a source file, the project documentation provided to you, or otherwise. Do not pull shit out of your ass.

## Half Steps 

Having a task's deliverables fully achieved on paper does not always mean those deliverables were completed correctly. Something like, being asked to complete a port, and outputting a technically complete port - that brute forces rendering in software, and as such runs terribly on real hardware. That's awful, benefits nobody, and wastes the users time and money. Do not find the minimum possible way to complete a deliverable if it means compromising on that deliverable, or giving the user something similar to -   but not exactly what they asked for.
