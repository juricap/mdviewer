# CHM to Markdown Conversion Guide

This document describes how to properly convert extracted CHM files to clean, readable markdown.

## Prerequisites

- `pandoc` - for HTML to markdown conversion
- `iconv` - for character encoding conversion
- `dos2unix` - for line ending fixes

## Common Issues with CHM Extractions

1. **Encoding Issues**: CHM files often use `iso-8859-1` encoding, but pandoc expects UTF-8
2. **HTML Artifacts**: Raw pandoc conversion includes CSS classes and formatting artifacts
3. **Code Formatting**: Code blocks and examples need special handling
4. **Structure**: Content needs proper markdown hierarchy and navigation

## Step-by-Step Conversion Process

### 1. Examine the File Structure

First, understand what files you have:
```bash
ls -la *.htm
```

Look for:
- `contents.htm` or table of contents
- `*.hhc` files (help contents)
- Individual topic files

### 2. Handle Encoding Issues

CHM files typically use `iso-8859-1` encoding. Convert before processing:
```bash
iconv -f iso-8859-1 -t utf-8 input.htm | pandoc -f html -t markdown
```

### 3. Clean HTML Content

Extract only the body content and remove HTML artifacts:
```bash
# Function to clean HTML content
clean_html_content() {
    local file="$1"
    iconv -f iso-8859-1 -t utf-8 "$file" | \
    sed -n '/<BODY>/,/<\/BODY>/p' | \
    sed -e 's/<[^>]*>//g' \
        -e 's/&nbsp;/ /g' \
        -e 's/&lt;/</g' \
        -e 's/&gt;/>/g' \
        -e 's/&amp;/\&/g' \
        -e 's/&quot;/"/g' \
        -e '/^[[:space:]]*$/d' \
        -e 's/^[[:space:]]*//' \
        -e 's/[[:space:]]*$//'
}
```

### 4. Format Code Blocks Properly

Identify and format code sections:
```bash
# AWK script to format code and parameters
format_content() {
    local file="$1"
    clean_html_content "$file" | \
    awk '
    BEGIN { in_declaration = 0; in_params = 0 }
    /^Declaration:/ { 
        print "\n**Declaration:**\n"; 
        in_declaration = 1; 
        next 
    }
    /^Description of parameters:/ { 
        print "\n**Parameters:**\n"; 
        in_params = 1; 
        next 
    }
    /^Return value:/ { 
        print "\n**Return value:**\n"; 
        in_params = 0; 
        next 
    }
    /^Remarks:/ { print "\n**Remarks:**\n"; next }
    /^Notes:/ { print "\n**Notes:**\n"; next }
    /^Examples?:/ { print "\n**Examples:**\n"; next }
    
    # Format C function declarations
    in_declaration && /^[A-Z]/ { 
        print "```c\n" $0 "\n```\n"; 
        in_declaration = 0; 
        next 
    }
    
    # Format parameter lists
    in_params && /^[a-zA-Z_][a-zA-Z0-9_]*[ ]*/ { 
        print "- **" $1 "** - " substr($0, length($1)+2); 
        next 
    }
    
    # Default: print line as-is
    { print }
    '
}
```

### 5. Create Structured Output

Build the final markdown with proper structure:
```bash
#!/bin/bash

output_file="converted_documentation.md"

# Create header with TOC
cat > "$output_file" << 'EOF'
# Documentation Title

*Copyright notice if applicable*

## Table of Contents

1. [Overview](#overview)
2. [Functions](#functions)
   - [Mandatory Functions](#mandatory-functions)
   - [Optional Functions](#optional-functions)
3. [Structures](#structures)
4. [Additional Sections](#additional-sections)

EOF

# Process files in logical order
declare -a files=(
    "overview.htm:## Overview"
    "function1.htm:## Functions\n### Function1"
    "function2.htm:### Function2"
    # Add more files as needed
)

for item in "${files[@]}"; do
    file="${item%%:*}"
    header="${item#*:}"
    
    if [ -f "$file" ]; then
        echo "Processing $file..."
        echo -e "\n$header\n" >> "$output_file"
        format_content "$file" >> "$output_file"
        echo -e "\n---\n" >> "$output_file"
    fi
done
```

## Best Practices

### 1. File Organization
- Process files in logical order (overview first, then functions, etc.)
- Use consistent section headers
- Include table of contents with working links

### 2. Code Formatting
- Wrap function declarations in \`\`\`c code blocks
- Format parameters as bullet lists with bold names
- Preserve example code with proper indentation

### 3. Content Cleanup
- Remove CSS class references (`{.p1}`, `{.s12}`, etc.)
- Clean up escaped characters (`\'` → `'`)
- Remove empty lines and normalize spacing

### 4. Structure
- Use proper markdown hierarchy (##, ###, ####)
- Add horizontal rules (---) between major sections
- Include navigation aids

## Example Script Template

```bash
#!/bin/bash

# CHM to Markdown Converter
output_file="documentation.md"

# Header function
create_header() {
    cat > "$output_file" << 'EOF'
# Your Documentation Title

*Copyright notice*

## Table of Contents
[Add your TOC here]

EOF
}

# Content processing function
process_file() {
    local file="$1"
    local title="$2"
    
    if [ -f "$file" ]; then
        echo -e "\n$title\n" >> "$output_file"
        format_content "$file" >> "$output_file"
        echo -e "\n---\n" >> "$output_file"
    fi
}

# Main conversion
create_header

# Process your files
process_file "overview.htm" "## Overview"
process_file "functions.htm" "## Functions"
# Add more files...

echo "Conversion completed: $output_file"
```

## Common Pitfalls to Avoid

1. **Don't skip encoding conversion** - Always handle iso-8859-1 to UTF-8
2. **Don't ignore code formatting** - Properly format function declarations and examples
3. **Don't forget structure** - Include proper headers and navigation
4. **Don't leave artifacts** - Clean up HTML classes and formatting remnants
5. **Don't rush parameter formatting** - Take time to format parameter lists properly

## Validation

After conversion, check:
- [ ] All code blocks are properly formatted
- [ ] Parameters are clearly listed
- [ ] No HTML artifacts remain
- [ ] Table of contents links work
- [ ] Examples are readable
- [ ] Structure is logical and navigable