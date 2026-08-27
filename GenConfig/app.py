#!/usr/bin/env python3
"""Proteo JSON configuration builder — local Flask app."""

import json
import os
from pathlib import Path

from flask import Flask, jsonify, render_template, request

from expand_multi import preview_expansions, validate_multi_syntax
from schema import default_config
from stage_rules import sanitize_config
from validator import validate_config_full

APP_DIR = Path(__file__).resolve().parent
PROTEO_HOME = APP_DIR.parent

app = Flask(__name__, static_folder="static", template_folder="templates")


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/blank")
def api_blank():
    return jsonify(default_config())


@app.route("/api/template")
def api_template():
    sample = PROTEO_HOME / "Codes" / "test.json"
    if sample.is_file():
        with open(sample, encoding="utf-8") as handle:
            return jsonify(json.load(handle))
    return jsonify(default_config())


@app.route("/api/validate", methods=["POST"])
def api_validate():
    config = request.get_json(silent=True)
    if not isinstance(config, dict):
        return jsonify({
            "valid": False,
            "errors": ["Root value must be a JSON object"],
            "warnings": [],
            "sanitized": config,
        })
    sanitized = sanitize_config(config)
    result = validate_config_full(sanitized)
    return jsonify({
        "valid": result["valid"],
        "errors": result["errors"],
        "warnings": result["warnings"],
        "sanitized": sanitized,
    })


@app.route("/api/schema")
def api_schema():
    from schema import (
        CAPTURE_METHOD_OPTIONS,
        DIST_OPTIONS,
        GENERAL_FIELDS,
        GROUP_FIELDS,
        MULTI_MODE_HINTS,
        REDISTRIBUTION_METHODS,
        REDISTRIBUTION_STRATEGIES,
        REDISTRIBUTION_STRATEGY_OPTIONS,
        RIGID_OPTIONS,
        SPAWN_METHODS,
        SPAWN_STRATEGIES,
        SPAWN_STRATEGY_OPTIONS,
        STAGE_OPTIONAL_FIELDS,
        STAGE_TIME_CAPPED_OPTIONS,
        STAGE_TYPE_HINTS,
        STAGE_TYPES,
    )

    return jsonify({
        "stage_types": STAGE_TYPES,
        "stage_type_hints": STAGE_TYPE_HINTS,
        "stage_time_capped_options": STAGE_TIME_CAPPED_OPTIONS,
        "spawn_methods": SPAWN_METHODS,
        "spawn_strategies": SPAWN_STRATEGIES,
        "spawn_strategy_options": SPAWN_STRATEGY_OPTIONS,
        "redistribution_methods": REDISTRIBUTION_METHODS,
        "redistribution_strategies": REDISTRIBUTION_STRATEGIES,
        "redistribution_strategy_options": REDISTRIBUTION_STRATEGY_OPTIONS,
        "dist_options": DIST_OPTIONS,
        "rigid_options": RIGID_OPTIONS,
        "capture_method_options": CAPTURE_METHOD_OPTIONS,
        "general_fields": GENERAL_FIELDS,
        "stage_optional_fields": STAGE_OPTIONAL_FIELDS,
        "group_fields": GROUP_FIELDS,
        "multi_mode_hints": MULTI_MODE_HINTS,
    })


@app.route("/api/multi/validate", methods=["POST"])
def api_multi_validate():
    config = request.get_json(silent=True)
    if not isinstance(config, dict):
        return jsonify({
            "valid": False,
            "errors": ["Root value must be a JSON object"],
            "warnings": [],
        })
    result = validate_multi_syntax(config)
    return jsonify(result)


@app.route("/api/multi/preview", methods=["POST"])
def api_multi_preview():
    config = request.get_json(silent=True)
    if not isinstance(config, dict):
        return jsonify({
            "syntax_valid": False,
            "syntax_errors": ["Root value must be a JSON object"],
            "syntax_warnings": [],
            "variant_axes": 0,
            "theoretical_combinations": 0,
            "valid_outputs": 0,
            "skipped_procs_filter": 0,
            "skipped_invalid": 0,
        })
    return jsonify(preview_expansions(config, validate_outputs=True))


if __name__ == "__main__":
    host = os.environ.get("CONFIG_GEN_HOST", "127.0.0.1")
    port = int(os.environ.get("CONFIG_GEN_PORT", "5000"))
    print(f"Proteo Config-Gen — PROTEO_HOME={PROTEO_HOME}")
    print(f"Open http://{host}:{port}")
    app.run(host=host, port=port, debug=True)
