# Workflows

Workflows are declarative automations defined in YAML or JSON. Drop a `.yaml` or `.yml` file in `.rig/extensions/` and it loads as an extension.

## Structure

```yaml
name: my-workflow
description: What this workflow does
defaults:
  some_var: default_value
steps:
  - name: step-one
    type: prompt
    prompt: "Analyze this: ${some_var}"
    save_as: analysis
  - name: step-two
    type: bash
    command: "echo done"
```

### Top-Level Fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Workflow identifier |
| `description` | no | Human-readable description |
| `trigger` | no | Trigger condition |
| `defaults` | no | Default variable values |
| `steps` | yes | Ordered list of steps |

## Step Types

16 step types are available:

| Type | Purpose |
|------|---------|
| `prompt` | Send text to the AI and get a response |
| `tool` | Execute a specific tool with arguments |
| `bash` | Run a shell command |
| `gate` | Pause for user approval before continuing |
| `condition` | Evaluate an expression and jump to different steps |
| `parallel` | Run multiple steps concurrently |
| `join` | Wait for parallel steps to finish |
| `sub_workflow` | Run another workflow file |
| `transform` | Modify variables |
| `loop` | Repeat a step over items in a list |
| `retry` | Re-run a step on failure (up to a limit) |
| `emit` | Publish an event for extensions |
| `wait_event` | Pause until a specific event fires |
| `spawn_session` | Start a new conversation session |
| `http` | Make an HTTP request |
| `checkpoint` | Save progress for resumption |

## Common Step Fields

Every step can use these fields:

| Field | Default | Description |
|-------|---------|-------------|
| `name` | required | Step identifier |
| `type` | auto-detected | Step type |
| `save_as` | - | Store output in a variable |
| `condition` | - | Skip step if expression is empty/false |
| `then` | - | Jump target if condition is true |
| `else` | - | Jump target if condition is false |
| `goto` | - | Loop back to a named step |
| `max_iterations` | 10 | Iteration limit for goto loops |
| `max_retries` | 0 | Retry attempts on failure |
| `retry_delay_ms` | 1000 | Delay between retries (milliseconds) |
| `timeout_ms` | 0 | Step timeout (0 = no limit) |
| `depends_on` | [] | Steps that must complete first |

## Variable Interpolation

Use `${variable_name}` to insert variable values into step fields:

```yaml
- name: greet
  type: prompt
  prompt: "Hello ${user_name}, analyze ${file_path}"
  save_as: greeting
```

### Special Variables

| Pattern | Resolves to |
|---------|------------|
| `${env.VARNAME}` | Environment variable |
| `${stepname.status}` | Step execution status: `success`, `error`, `skipped`, `running`, `pending` |
| `${varname.field}` | Field of an object variable |
| `${loop.item}` | Current item in a loop |
| `${loop.index}` | Current index in a loop |

Object/array variables are serialized to JSON when interpolated into strings.

## Step Type Details

### prompt

```yaml
- name: analyze
  type: prompt
  prompt: "Explain this code"
  model: claude-sonnet-4-6
  save_as: explanation
```

### bash

```yaml
- name: build
  type: bash
  config:
    command: "make clean && make"
  save_as: build_output
```

### condition

```yaml
- name: check-result
  type: condition
  condition: "build_output.status == 'success'"
  then: deploy
  else: fix-errors
```

### parallel

```yaml
- name: gather-info
  type: parallel
  steps:
    - name: get-tests
      type: bash
      config:
        command: "make test"
      save_as: test_results
    - name: get-lint
      type: bash
      config:
        command: "make lint"
      save_as: lint_results
```

Each parallel step runs in its own thread with an independent copy of variables. Results are merged back after all steps complete.

### loop

```yaml
- name: process-files
  type: loop
  loop_over: file_list
  loop_body:
    name: process-one
    type: bash
    config:
      command: "wc -l ${loop.item}"
```

### gate

```yaml
- name: confirm-deploy
  type: gate
  config:
    gate: "About to deploy to production. Continue?"
```

Pauses execution and asks the user for approval.

### http

```yaml
- name: fetch-data
  type: http
  http:
    method: POST
    url: "https://api.example.com/data"
    headers:
      Authorization: "Bearer ${api_token}"
    body: '{"query": "${search_term}"}'
    timeout_ms: 30000
  save_as: api_response
```

### checkpoint

```yaml
- name: save-progress
  type: checkpoint
```

Saves the current variable state, step statuses, and iteration counts. If the workflow is interrupted, it resumes from the last checkpoint.

### retry

```yaml
- name: flaky-operation
  type: retry
  max_retries: 3
  retry_delay_ms: 2000
  loop_body:
    name: attempt
    type: bash
    config:
      command: "curl https://unreliable-api.com/data"
```

## Expressions

Used in `condition` fields. Supports:

| Operator | Example |
|----------|---------|
| `==` | `status == 'success'` |
| `!=` | `result != 'failed'` |
| `>`, `<`, `>=`, `<=` | `count > '5'` |
| `contains` | `output contains 'error'` |
| `AND` | `a == 'x' AND b == 'y'` |
| `OR` | `a == 'x' OR b == 'y'` |
| `NOT` | `NOT failed` |
| Parentheses | `(a OR b) AND c` |

String literals use single quotes. Operators (AND, OR, NOT) are case-insensitive.

A bare variable name is truthy if it exists, is non-empty, and is not `"false"` or `"0"`.
