import random

topics = [
    ("Deep Work", "productivity", "focus"),
    ("Learning to Code", "programming", "software"),
    ("Building Habits", "routine", "discipline"),
    ("Remote Work", "home office", "collaboration"),
    ("Writing Every Day", "creativity", "journaling"),
    ("Reading More Books", "knowledge", "curiosity"),
    ("Side Projects", "shipping", "motivation"),
    ("Health and Fitness", "exercise", "energy"),
    ("Mindfulness", "meditation", "awareness"),
    ("Financial Independence", "savings", "investing"),
    ("Teaching Others", "mentoring", "communication"),
    ("Open Source", "community", "contribution"),
    ("Time Management", "priorities", "scheduling"),
    ("Decision Making", "clarity", "tradeoffs"),
    ("System Design", "architecture", "scalability"),
]

actions = [
    "I spent the morning working on", "After lunch I dove into",
    "This week I focused on", "I made real progress with",
    "The breakthrough came when I started", "I finally understood",
    "My approach to", "The hardest part of", "What surprised me about",
    "I need to rethink my strategy for", "Looking back at my work on",
    "The most rewarding part of", "I struggled with", "I discovered that",
    "One thing that changed everything was", "I realized that",
]

reflections = [
    "It feels like real progress.", "I'm not sure this is sustainable though.",
    "Tomorrow I want to push further.", "The key was consistency.",
    "Small steps compound over time.", "I wish I had started sooner.",
    "The results speak for themselves.", "This needs more thought.",
    "I'm cautiously optimistic.", "The data supports this approach.",
    "Others have noticed the difference.", "This is becoming second nature.",
    "I need to be more patient with the process.", "Momentum is building.",
    "The tradeoffs are worth it.", "I should document this more carefully.",
    "There's still a lot to figure out.", "But the direction feels right.",
    "The community has been incredibly helpful.", "This changes everything.",
]

observations = [
    "The weather was perfect for a long walk and some thinking.",
    "I had three uninterrupted hours which made all the difference.",
    "A conversation with a friend gave me a completely new perspective.",
    "I found an old notebook with ideas that are suddenly relevant again.",
    "The morning routine is finally clicking into place.",
    "I read an article that challenged my assumptions about this.",
    "Sometimes the best thing to do is step away from the screen.",
    "I tried a different approach today and it worked surprisingly well.",
    "The tools matter less than I thought. Process matters more.",
    "I keep coming back to the same core principles.",
    "Constraints breed creativity. Fewer options led to better decisions.",
    "I set a timer for 25 minutes and was shocked how much I got done.",
    "Saying no to three things made room for the one thing that mattered.",
    "I paired with someone today and we solved it in half the time.",
    "The problem wasn't technical. It was a communication gap.",
]

section_titles = [
    "What worked", "What didn't work", "Lessons learned",
    "Key takeaways", "Next steps", "Reflections",
    "Things I want to try", "Open questions", "Highlights",
    "Challenges", "Wins", "Observations",
]

random.seed(42)
lines = []
week = 1
day_names = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]

while len(lines) < 100000:
    topic, kw1, kw2 = random.choice(topics)

    # Week header
    lines.append(f"# Week {week}: {topic}")
    lines.append("")

    hours = random.randint(6, 20)
    lines.append(f"A total of {hours} hours of focused work this week. {random.choice(reflections)}")
    lines.append("")

    # Daily entries
    for day in range(random.randint(3, 7)):
        day_name = day_names[day % 7]
        lines.append(f"### {day_name}")
        lines.append("")

        # A few paragraphs per day
        for _ in range(random.randint(1, 3)):
            sentences = []
            for _ in range(random.randint(3, 8)):
                r = random.random()
                if r < 0.4:
                    sentences.append(f"{random.choice(actions)} {kw1} and {kw2}. {random.choice(reflections)}")
                elif r < 0.7:
                    sentences.append(random.choice(observations))
                else:
                    sentences.append(f"{random.choice(actions)} {topic.lower()}. {random.choice(reflections)}")
            lines.append(" ".join(sentences))
            lines.append("")

        # Sometimes add a bullet list
        if random.random() < 0.4:
            section = random.choice(section_titles)
            lines.append(f"**{section}:**")
            lines.append("")
            for _ in range(random.randint(3, 6)):
                lines.append(f"- {random.choice(actions)} {kw1}. {random.choice(reflections)}")
            lines.append("")

    # Section summary
    lines.append(f"## {random.choice(section_titles)}")
    lines.append("")
    for _ in range(random.randint(2, 4)):
        sentences = []
        for _ in range(random.randint(4, 10)):
            sentences.append(f"{random.choice(actions)} {topic.lower()}. {random.choice(reflections)}")
        lines.append(" ".join(sentences))
        lines.append("")

    lines.append("---")
    lines.append("")
    week += 1

with open("test_prose_100k.txt", "w") as f:
    for line in lines[:100000]:
        f.write(line + "\n")

print(f"Generated {min(len(lines), 100000)} lines")
