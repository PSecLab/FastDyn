import os
import sys
import google.generativeai as genai

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <prompt_file> <output_file>")
    sys.exit(1)

prompt_file = sys.argv[1]
output_file = sys.argv[2]

# Load API key from environment
api_key = os.environ.get("GEMINI_API_KEY")
if not api_key:
    print("Error: GEMINI_API_KEY environment variable not set")
    sys.exit(1)

genai.configure(api_key=api_key)

# Read prompt
with open(prompt_file, "r", encoding="utf-8") as f:
    prompt_text = f.read()

# Create model
model = genai.GenerativeModel("gemini-2.0-flash")

# Generate response
response = model.generate_content(prompt_text)

# Save response
with open(output_file, "w", encoding="utf-8") as f:
    f.write(response.text)

print(f"Response saved to {output_file}")

