# FastDyn Unit Tests

## Framework
- **Test runner**: [pytest](https://docs.pytest.org/)
- **Mocking**: `unittest.mock` from the Python standard library

## Directory Layout

```
tests/unit/
  __init__.py
  conftest.py               # Shared pytest fixtures
  test_response_parser.py   # Tests for LLM response parsing
  test_patch.py             # Tests for SEARCH/REPLACE patch application
  test_llm_client.py        # Tests for LLM client (mocked, no real API calls)
  test_evaluate_metrics.py  # Tests for --evaluate metrics (LLMCallMetrics, JSONL output)
  test_ablation_flags.py    # Tests for --no-encoder, --no-vio, --no-rca ablation flags
```

## How to Run

Run all unit tests:
```bash
pytest tests/unit/ -v
```

Run a specific test file:
```bash
pytest tests/unit/test_patch.py -v
```

Run a specific test function:
```bash
pytest tests/unit/test_patch.py::test_single_patch_success -v
```

## Conventions

### Naming
- Test files: `test_<module_name>.py`
- Test functions: `test_<descriptive_name>` (use underscores, be specific)
- Test classes (optional): `TestClassName` when grouping related tests

### Fixtures
- Shared fixtures live in `conftest.py`
- Module-specific fixtures can live at the top of the test file
- Use `@pytest.fixture` decorator, not manual setup/teardown

### Mocking
- **Never make real API calls** in unit tests
- Use `unittest.mock.patch` or `unittest.mock.MagicMock` for external dependencies
- Mock at the boundary (e.g., mock the `openai.OpenAI` client, not internal functions)

### Assertions
- Use plain `assert` statements (pytest rewrites them for clear failure messages)
- Use `pytest.raises(ExceptionType)` for expected exceptions

### Test Data
- Inline small test data directly in the test function
- For larger fixtures (e.g., sample LLM responses), use multi-line strings or
  files in a `testdata/` subdirectory

## Adding New Tests

1. Create a new file `test_<module>.py` in this directory
2. Import the module under test
3. Write test functions prefixed with `test_`
4. Run `pytest tests/unit/ -v` to verify
