import os, csv, random, json, threading, math
import urllib.request
import urllib.error
import re
import ast
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from tkinter.scrolledtext import ScrolledText

try:
    import openai
except ImportError:
    openai = None

CSV_FIELDS = [
    "Name", "NodeId", "PosX", "PosY", "PosZ", "ZoneId", "RoomType", "RoomRole", 
    "Difficulty", "CombatWeight", "BotProfile", "EnemyPreset", "LootLevel", "RoomSize", "Connections", 
    "NodeTags", "RoomPrefabId", "SpawnCount", "LootCount", "EnvType", "Theme", "ObstacleDensity", "Seed", "LootStrategy",
    "EnterableBuildingRatio", "TraversalLaneSeeds"
]

VALID_ROOM_TYPES    = ["Start", "Normal", "Combat", "Loot", "Boss", "Exit"]
VALID_ENV_TYPES     = ["Urban", "Jungle", "NatureVillage"]
VALID_ENEMY_PRESETS = ["Scavenger", "Raider", "BossGuard", "Sniper", "Default", "None"]
VALID_LOOT_LEVELS   = ["Low", "Medium", "High", "Common", "Epic"]
VALID_LOOT_STRAT    = ["Central_Cache", "Hidden_In_Corners", "Scattered"]
VALID_BOT_PROFILES  = ["Aggressive", "Tactical", "Defensive"]
ROOM_SIZE_VALUES    = ["Small", "Medium", "Large", "Massive"]

DIFFICULTY_MAP = {
    "easy": 0.8, "normal": 1.0, "hard": 1.6, "extreme": 2.2,
    "쉬움": 0.8, "보통": 1.0, "어려움": 1.6, "매우어려움": 2.2
}

MIN_CONNECTED_DIST_UU = 2000.0    
TARKOV_GRID_SIZE_UU = 5200.0  

# 🔥 [고퀄리티 프롬프트 프리셋 탑재] 게임성이 돋보이는 한글/영문 템플릿
PROMPT_TEMPLATES = {
    "Custom (직접 입력)": "",
    "🌍 [오픈월드] 대규모 거점 탈환 (20 Rooms)": "오픈월드 스타일의 거대 맵을 생성해줘. 중앙에는 안전 지대(Start)를 배치하고, 외곽으로 뻗어나갈수록 교전(Combat) 난이도와 적(Raider, Sniper)의 밀도가 올라가게 해줘. 곳곳에 대형 보급 구역(Loot)을 배치하고 가장 깊은 곳에 보스(Boss)와 탈출구(Exit)를 둬. RoomSize는 Massive와 Large를 많이 섞어줘.",
    "🌲 [오픈월드] 정글 파밍 캠프 (12 Rooms)": "자연/정글 테마(Jungle)의 파밍 위주 오픈월드를 만들어줘. 전투는 가벼운 Scavenger 위주로 배치하고, 전리품(Loot) 구역을 많이 배치해서 탐험과 파밍의 재미를 극대화해줘. 보스방은 생략하거나 약하게 설정해.",
    "🏢 [타르코프] 하드코어 실내 교전 (15 Rooms)": "타르코프 느낌의 좁고 복잡한 도심(Urban) 실내전 맵을 만들어줘. 방의 크기는 Small과 Medium 위주로 하고, Tactical 프로필을 가진 적들이 코너에 매복(Hidden_In_Corners)하게 설정해. 보상은 하이 티어(Epic)로 설정해서 긴장감을 높여줘.",
    "💀 [타르코프] 극한의 보스 런 (10 Rooms)": "극악의 난이도를 자랑하는 폐쇄형 하드코어 맵을 생성해줘. 보스 가드(BossGuard)들이 방마다 배치되어 있고, 플레이어가 쉴 틈 없이 전투를 치러야 해. RoomSize는 좁은 Small 위주로 구성해."
}

class MasterArchitect(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Master Architect - Advanced Level Generator")
        self.geometry("580x1050")
        self.resizable(False, False)
        
        self.bg_color, self.panel_color = "#18181B", "#27272A"
        self.text_color, self.sub_text_color = "#F4F4F5", "#71717A"
        self.accent_color, self.accent_hover = "#10B981", "#059669"
        self.log_color = "#00FF00"
        self.disabled_color = "#3F3F46"
        self.configure(bg=self.bg_color)
        
        self.out_dir_var = tk.StringVar(value=os.path.join(os.path.expanduser("~"), "Desktop"))
        self.room_count_var = tk.IntVar(value=15)
        self.difficulty_var = tk.StringVar(value="Normal")
        self.api_key_var = tk.StringVar(value="")
        self.ai_mode_var = tk.StringVar(value="Ollama (Local AI)")
        self.ollama_model_var = tk.StringVar(value="llama3.1")
        self.template_var = tk.StringVar(value="Custom (직접 입력)")
        
        self.layout_style_var = tk.StringVar(value="Open World (Radial & Terrain)")
        self.map_size_x_var = tk.IntVar(value=1000)
        self.map_size_y_var = tk.IntVar(value=1000)
        self._active_target_room_count = self._safe_int(self.room_count_var.get(), default_value=15, min_value=5, max_value=50)
        self._generation_directives = {"boss_policy": "required", "prefer_weak_boss": False, "loot_ratio": 0.2, "loot_min": 1}

        self._apply_styles()
        self._build_ui()
        self._update_ui_state()

    def _apply_styles(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure("Flat.TCombobox", fieldbackground=self.panel_color, background=self.panel_color, foreground=self.text_color, bordercolor=self.bg_color, arrowcolor=self.text_color)
        style.map("Flat.TCombobox", fieldbackground=[('readonly', self.panel_color), ('disabled', self.bg_color)], foreground=[('readonly', self.text_color), ('disabled', self.sub_text_color)])
        self.option_add('*TCombobox*Listbox.background', self.panel_color)
        self.option_add('*TCombobox*Listbox.foreground', self.text_color)
        self.option_add('*TCombobox*Listbox.selectBackground', self.accent_color)
        self.option_add('*TCombobox*Listbox.font', ("Segoe UI", 10))
        style.configure("Flat.Horizontal.TScale", background=self.bg_color, troughcolor=self.panel_color)

    def _build_ui(self):
        container = tk.Frame(self, bg=self.bg_color, padx=30, pady=15)
        container.pack(fill="both", expand=True)
        
        tk.Label(container, text="LEVEL ARCHITECT", font=("Segoe UI Black", 22), fg=self.text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 2))
        tk.Label(container, text="Hybrid Generation: Open World & CQB", font=("Segoe UI", 10), fg=self.sub_text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 15))

        tk.Label(container, text="Layout Style", font=("Segoe UI", 9, "bold"), fg=self.accent_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 5))
        self.style_combo = ttk.Combobox(container, textvariable=self.layout_style_var, style="Flat.TCombobox", values=["Open World (Radial & Terrain)", "Tarkov Style (Tight Grid)"], state="readonly", font=("Segoe UI", 10))
        self.style_combo.pack(fill="x", pady=(0, 10), ipady=4)

        tk.Label(container, text="Generation Mode", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 5))
        self.mode_combo = ttk.Combobox(container, textvariable=self.ai_mode_var, style="Flat.TCombobox", values=["Ollama (Local AI)", "OpenAI (Cloud AI)", "Offline (Algorithm)"], state="readonly", font=("Segoe UI", 10))
        self.mode_combo.pack(fill="x", pady=(0, 10), ipady=4)
        self.mode_combo.bind("<<ComboboxSelected>>", self._update_ui_state)

        self.lbl_count_title = tk.Label(container, text="Target Room Count (AI + Offline)", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w")
        self.lbl_count_title.pack(fill="x", pady=(0, 5))
        count_frame = tk.Frame(container, bg=self.bg_color)
        count_frame.pack(fill="x", pady=(0, 10))
        self.lbl_count_val = tk.Label(count_frame, textvariable=self.room_count_var, font=("Segoe UI", 11, "bold"), fg=self.accent_color, bg=self.bg_color, width=3)
        self.lbl_count_val.pack(side="right")
        self.scale_widget = ttk.Scale(count_frame, from_=5, to=50, variable=self.room_count_var, orient="horizontal", style="Flat.Horizontal.TScale", command=lambda s: self.room_count_var.set(int(float(s))))
        self.scale_widget.pack(side="left", fill="x", expand=True, padx=(0, 10))

        tk.Label(container, text="Max Map Size Constraint (Meters)", font=("Segoe UI", 9, "bold"), fg=self.sub_text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 5))
        size_frame = tk.Frame(container, bg=self.bg_color)
        size_frame.pack(fill="x", pady=(0, 15))
        size_frame.columnconfigure(0, weight=1)
        size_frame.columnconfigure(1, weight=1)
        tk.Label(size_frame, text="Width (X) m:", font=("Segoe UI", 9), fg=self.sub_text_color, bg=self.bg_color).grid(row=0, column=0, sticky="w")
        tk.Entry(size_frame, textvariable=self.map_size_x_var, bg=self.panel_color, fg=self.text_color, insertbackground=self.text_color, relief="flat").grid(row=1, column=0, sticky="ew", padx=(0,5), ipady=5)
        tk.Label(size_frame, text="Height (Y) m:", font=("Segoe UI", 9), fg=self.sub_text_color, bg=self.bg_color).grid(row=0, column=1, sticky="w", padx=(5,0))
        tk.Entry(size_frame, textvariable=self.map_size_y_var, bg=self.panel_color, fg=self.text_color, insertbackground=self.text_color, relief="flat").grid(row=1, column=1, sticky="ew", padx=(5,0), ipady=5)

        grid_frame = tk.Frame(container, bg=self.bg_color)
        grid_frame.pack(fill="x", pady=(0, 10))
        grid_frame.columnconfigure(0, weight=1)
        grid_frame.columnconfigure(1, weight=1)
        self.lbl_api = tk.Label(grid_frame, text="OpenAI API Key", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w")
        self.lbl_api.grid(row=0, column=0, sticky="ew", padx=(0, 5), pady=(0, 5))
        self.api_key_entry = tk.Entry(grid_frame, textvariable=self.api_key_var, show="*", bg=self.panel_color, fg=self.text_color, relief="flat", font=("Segoe UI", 10))
        self.api_key_entry.grid(row=1, column=0, sticky="ew", padx=(0, 5), ipady=5)
        self.lbl_ollama = tk.Label(grid_frame, text="Ollama Model", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w")
        self.lbl_ollama.grid(row=0, column=1, sticky="ew", padx=(5, 0), pady=(0, 5))
        self.ollama_entry = tk.Entry(grid_frame, textvariable=self.ollama_model_var, bg=self.panel_color, fg=self.text_color, relief="flat", font=("Segoe UI", 10))
        self.ollama_entry.grid(row=1, column=1, sticky="ew", padx=(5, 0), ipady=5)

        self.lbl_prompt_title = tk.Label(container, text="AI Prompt (Director's Note)", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w")
        self.lbl_prompt_title.pack(fill="x", pady=(0, 5))
        self.template_combo = ttk.Combobox(container, textvariable=self.template_var, style="Flat.TCombobox", values=list(PROMPT_TEMPLATES.keys()), state="readonly", font=("Segoe UI", 9))
        self.template_combo.pack(fill="x", pady=(0, 5), ipady=3)
        self.template_combo.bind("<<ComboboxSelected>>", self._on_template_change)
        self.prompt_text = tk.Text(container, bg=self.panel_color, fg=self.text_color, insertbackground=self.text_color, relief="flat", font=("Segoe UI", 10), height=5)
        self.prompt_text.pack(fill="x", pady=(0, 10))
        
        tk.Label(container, text="Export Path", font=("Segoe UI", 9, "bold"), fg=self.text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(0, 5))
        path_frame = tk.Frame(container, bg=self.bg_color)
        path_frame.pack(fill="x", pady=(0, 15))
        tk.Entry(path_frame, textvariable=self.out_dir_var, bg=self.panel_color, fg=self.text_color, insertbackground=self.text_color, relief="flat", font=("Segoe UI", 10)).pack(side="left", fill="x", expand=True, ipady=5, padx=(0, 10))
        tk.Button(path_frame, text="DIR", bg=self.panel_color, fg=self.text_color, relief="flat", command=lambda: self.out_dir_var.set(filedialog.askdirectory())).pack(side="right", ipadx=10, ipady=3)

        self.btn_generate = tk.Button(container, text="GENERATE CSV", bg=self.accent_color, fg="#FFFFFF", relief="flat", font=("Segoe UI", 13, "bold"), cursor="hand2", command=self._start_generation)
        self.btn_generate.pack(fill="x", ipady=10)

        tk.Label(container, text="Generation Progress Log", font=("Segoe UI", 9, "bold"), fg=self.sub_text_color, bg=self.bg_color, anchor="w").pack(fill="x", pady=(15, 5))
        self.log_console = ScrolledText(container, bg="#000000", fg=self.log_color, font=("Consolas", 9), height=6, relief="flat", state="disabled")
        self.log_console.pack(fill="x")
        self._log("System Initialized. Ready to generate.")

    def _log(self, message):
        self.after(0, self._append_log, message)

    def _append_log(self, message):
        self.log_console.config(state="normal")
        self.log_console.insert("end", f"> {message}\n")
        self.log_console.see("end")
        self.log_console.config(state="disabled")

    def _update_ui_state(self, event=None):
        mode = self.ai_mode_var.get()
        is_ai = "Offline" not in mode
        # Room count must remain configurable in every mode.
        self.scale_widget.state(["!disabled"])
        self.lbl_count_val.config(fg=self.accent_color)
        self.lbl_count_title.config(fg=self.text_color)
        if not is_ai:
            self.template_combo.state(["disabled"])
            self.prompt_text.config(state="disabled", bg=self.bg_color, fg=self.sub_text_color)
            self.lbl_prompt_title.config(fg=self.sub_text_color)
            self.api_key_entry.config(state="disabled", bg=self.bg_color)
            self.ollama_entry.config(state="disabled", bg=self.bg_color)
            self.lbl_api.config(fg=self.sub_text_color)
            self.lbl_ollama.config(fg=self.sub_text_color)
        else:
            self.template_combo.state(["readonly"])
            self.prompt_text.config(state="normal", bg=self.panel_color, fg=self.text_color)
            self.lbl_prompt_title.config(fg=self.text_color)
            if "OpenAI" in mode:
                self.api_key_entry.config(state="normal", bg=self.panel_color)
                self.lbl_api.config(fg=self.text_color)
                self.ollama_entry.config(state="disabled", bg=self.bg_color)
                self.lbl_ollama.config(fg=self.sub_text_color)
            else:
                self.api_key_entry.config(state="disabled", bg=self.bg_color)
                self.lbl_api.config(fg=self.sub_text_color)
                self.ollama_entry.config(state="normal", bg=self.panel_color)
                self.lbl_ollama.config(fg=self.text_color)

    def _on_template_change(self, event=None):
        selected_key = self.template_var.get()
        prompt_text = PROMPT_TEMPLATES.get(selected_key, "")
        self.prompt_text.delete("1.0", tk.END)
        self.prompt_text.insert("1.0", prompt_text)

    def _start_generation(self):
        mode = self.ai_mode_var.get()
        api_key = self.api_key_var.get().strip()
        ollama_model = self.ollama_model_var.get().strip()
        user_prompt = self.prompt_text.get("1.0", "end-1c").strip()
        is_ai_mode = "Offline" not in mode
        resolved_count, count_source = self._resolve_target_room_count(is_ai_mode, user_prompt)
        self._active_target_room_count = resolved_count
        self._generation_directives = self._resolve_generation_directives(is_ai_mode, user_prompt)
        if is_ai_mode and count_source == "prompt":
            self._log(f"[Room Count] AI prompt specified {resolved_count}. Slider value ignored.")
        else:
            self._log(f"[Room Count] Target set to {resolved_count} (source: {count_source}).")
        self._log(f"[Directives] boss={self._generation_directives.get('boss_policy')} loot_ratio={self._generation_directives.get('loot_ratio'):.2f}")
        
        self.btn_generate.config(text="GENERATING... PLEASE WAIT", bg=self.disabled_color, state="disabled")
        self._log("=========================================")
        
        if "OpenAI" in mode:
            threading.Thread(target=self._run_openai_generation, args=(api_key, user_prompt), daemon=True).start()
        elif "Ollama" in mode:
            threading.Thread(target=self._run_ollama_generation, args=(ollama_model, user_prompt), daemon=True).start()
        else:
            threading.Thread(target=self._run_local_generation, daemon=True).start()

    # 🔥 [수정] AI가 RoomSize를 숫자로 적는 것을 완전히 금지하는 강력한 시스템 프롬프트
    def _get_system_prompt(self, is_ai=True):
        target_count = self._get_active_target_room_count()
        count_instruction = f"Output EXACTLY {target_count} room objects."
        return f"""
        You are a strict JSON data generator for an open world game level.
        Output ONLY a JSON object with a key "rooms" containing an array of room objects.
        CRITICAL RULES:
        1. ROOM COUNT: {count_instruction}
        2. NODE IDs: 0 to N-1. Start=0, Exit=Last, Boss=Near End.
        3. POSITION PLACEHOLDERS ARE OK: PosX/PosY/PosZ can be 0.0 (tool will auto-place coordinates later).
        4. REQUIRED FIELDS: {', '.join(CSV_FIELDS)}
        5. RoomSize MUST BE EXACTLY ONE OF THESE STRINGS: "Small", "Medium", "Large", "Massive". DO NOT USE NUMBERS!
        Base Difficulty is {self.difficulty_var.get()}.
        """

    def _clean_json_string(self, raw_str):
        raw_str = raw_str.strip()
        start_idx = raw_str.find('{')
        end_idx = raw_str.rfind('}')
        if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
            return raw_str[start_idx:end_idx+1]
        return raw_str

    def _strip_markdown_fences(self, text):
        raw = str(text or "").strip()
        if raw.startswith("```") and raw.endswith("```"):
            raw = re.sub(r"^```(?:json)?\s*", "", raw, flags=re.IGNORECASE)
            raw = re.sub(r"\s*```$", "", raw)
            return raw.strip()

        fenced = re.findall(r"```(?:json)?\s*(.*?)```", raw, flags=re.IGNORECASE | re.DOTALL)
        if fenced:
            return fenced[0].strip()
        return raw

    def _repair_common_json_issues(self, text):
        fixed = str(text or "")
        fixed = fixed.replace("\ufeff", "")
        fixed = fixed.replace("\r\n", "\n")
        fixed = fixed.replace("“", "\"").replace("”", "\"")
        fixed = fixed.replace("’", "'")
        fixed = re.sub(r",(\s*[}\]])", r"\1", fixed)
        fixed = re.sub(r"}\s*{", "},{", fixed)
        fixed = re.sub(r"]\s*\[", "],[", fixed)
        fixed = re.sub(r"\bTrue\b", "true", fixed)
        fixed = re.sub(r"\bFalse\b", "false", fixed)
        fixed = re.sub(r"\bNone\b", "null", fixed)
        return fixed.strip()

    def _extract_rooms_array_text(self, text):
        src = str(text or "")
        key_match = re.search(r'["\']rooms["\']\s*:\s*\[', src, flags=re.IGNORECASE)
        if not key_match:
            return None

        start = src.find("[", key_match.start())
        if start < 0:
            return None

        depth = 0
        in_string = False
        escape = False
        quote = ""
        for idx in range(start, len(src)):
            ch = src[idx]
            if in_string:
                if escape:
                    escape = False
                elif ch == "\\":
                    escape = True
                elif ch == quote:
                    in_string = False
                continue

            if ch == "\"" or ch == "'":
                in_string = True
                quote = ch
                continue
            if ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
                if depth == 0:
                    return src[start:idx+1]
        return None

    def _try_parse_level_data(self, candidate):
        last_error = None
        candidate = str(candidate or "").strip()
        if not candidate:
            return None, "empty candidate"

        try:
            parsed = json.loads(candidate)
            if isinstance(parsed, dict) and isinstance(parsed.get("rooms"), list):
                return parsed, None
        except Exception as e:
            last_error = str(e)

        try:
            parsed = ast.literal_eval(candidate)
            if isinstance(parsed, dict) and isinstance(parsed.get("rooms"), list):
                return {"rooms": list(parsed.get("rooms", []))}, None
        except Exception as e:
            if not last_error:
                last_error = str(e)

        arr_text = self._extract_rooms_array_text(candidate)
        if arr_text:
            for arr_candidate in [arr_text, self._repair_common_json_issues(arr_text)]:
                try:
                    arr_parsed = json.loads(arr_candidate)
                    if isinstance(arr_parsed, list):
                        return {"rooms": arr_parsed}, None
                except Exception:
                    pass
                try:
                    arr_parsed = ast.literal_eval(arr_candidate)
                    if isinstance(arr_parsed, list):
                        return {"rooms": arr_parsed}, None
                except Exception:
                    pass

        return None, last_error or "unknown parse failure"

    def _parse_level_data_from_ai_output(self, raw_text):
        raw = str(raw_text or "")
        candidates = []
        for base in [raw, self._strip_markdown_fences(raw), self._clean_json_string(raw)]:
            if not base:
                continue
            candidates.append(base)
            candidates.append(self._strip_markdown_fences(base))
            candidates.append(self._repair_common_json_issues(base))
            candidates.append(self._repair_common_json_issues(self._strip_markdown_fences(base)))

        seen = set()
        last_error = "no candidates"
        for candidate in candidates:
            cand = str(candidate or "").strip()
            if not cand or cand in seen:
                continue
            seen.add(cand)
            parsed, err = self._try_parse_level_data(cand)
            if parsed is not None:
                return parsed
            if err:
                last_error = err
        raise ValueError(f"AI JSON parse failed after recovery attempts: {last_error}")

    def _sanitize_value(self, val, valid_list, default_val):
        val_str = str(val).strip()
        if val_str in valid_list: return val_str 
        val_lower = val_str.lower()
        for v in valid_list:
            if v.lower() in val_lower: return v
        return default_val

    def _safe_float(self, value, default_value=0.0, min_value=None, max_value=None):
        try:
            raw = str(value).strip()
            if raw == "":
                parsed = float(default_value)
            else:
                parsed = float(raw)
        except Exception:
            parsed = float(default_value)
        if min_value is not None:
            parsed = max(min_value, parsed)
        if max_value is not None:
            parsed = min(max_value, parsed)
        return float(parsed)

    def _safe_int(self, value, default_value=0, min_value=None, max_value=None):
        try:
            raw = str(value).strip()
            if raw == "":
                parsed = int(default_value)
            else:
                if raw.startswith("[") and raw.endswith("]"):
                    raw = raw[1:-1].split(",")[0].strip()
                parsed = int(float(raw))
        except Exception:
            parsed = int(default_value)
        if min_value is not None:
            parsed = max(min_value, parsed)
        if max_value is not None:
            parsed = min(max_value, parsed)
        return int(parsed)

    def _normalize_difficulty(self, value):
        text = str(value).strip()
        if text == "":
            return 1.0
        mapped = DIFFICULTY_MAP.get(text.lower())
        if mapped is not None:
            return float(mapped)
        return self._safe_float(text, default_value=1.0, min_value=0.5, max_value=3.0)

    def _normalize_room_size(self, raw_size):
        raw_size = str(raw_size).strip()
        if raw_size.isdigit():
            val = int(raw_size)
            if val <= 60: return "Small"
            if val <= 110: return "Medium"
            if val <= 160: return "Large"
            return "Massive"
        return self._sanitize_value(raw_size, ROOM_SIZE_VALUES, "Medium")

    def _estimate_room_radius_uu(self, room_size):
        normalized = self._normalize_room_size(room_size)
        if normalized == "Small":
            return 1800.0
        if normalized == "Medium":
            return 2600.0
        if normalized == "Large":
            return 4200.0
        if normalized == "Massive":
            return 6200.0
        return 2600.0

    def _get_active_target_room_count(self):
        return self._safe_int(getattr(self, "_active_target_room_count", self.room_count_var.get()), default_value=15, min_value=5, max_value=50)

    def _extract_prompt_room_count(self, prompt_text):
        text = str(prompt_text or "").strip()
        if not text:
            return None

        patterns = [
            r'(\d+)\s*(?:개|개의)?\s*방',
            r'(\d+)\s*(?:개|개의)?\s*룸',
            r'(?:방|룸)\s*(?:은|는|:)?\s*(\d+)\s*개',
            r'room\s*count\s*[:=]?\s*(\d+)',
            r'(\d+)\s*rooms?',
            r'총\s*(\d+)\s*개',
        ]
        for pattern in patterns:
            m = re.search(pattern, text, flags=re.IGNORECASE)
            if m:
                try:
                    return int(m.group(1))
                except Exception:
                    return None

        number_tokens = [int(n) for n in re.findall(r'\d+', text)]
        filtered = [n for n in number_tokens if 5 <= n <= 50]
        if len(filtered) == 1:
            return filtered[0]
        return None

    def _resolve_target_room_count(self, is_ai_mode, user_prompt):
        slider_count = self._safe_int(self.room_count_var.get(), default_value=15, min_value=5, max_value=50)
        if not is_ai_mode:
            return slider_count, "slider"

        prompt_count = self._extract_prompt_room_count(user_prompt)
        if prompt_count is None:
            return slider_count, "slider"

        return self._safe_int(prompt_count, default_value=slider_count, min_value=5, max_value=50), "prompt"

    def _default_generation_directives(self):
        return {"boss_policy": "required", "prefer_weak_boss": False, "loot_ratio": 0.2, "loot_min": 1}

    def _get_generation_directives(self):
        raw = getattr(self, "_generation_directives", None)
        if isinstance(raw, dict):
            return raw
        return self._default_generation_directives()

    def _resolve_generation_directives(self, is_ai_mode, user_prompt):
        directives = self._default_generation_directives()
        if not is_ai_mode:
            return directives

        directives["boss_policy"] = "optional"
        text = str(user_prompt or "").lower()
        has_boss_word = ("boss" in text) or ("보스" in text)
        has_skip_word = ("omit" in text) or ("skip" in text) or ("생략" in text)
        has_weak_word = ("weak" in text) or ("light" in text) or ("약" in text)
        has_required_word = ("must" in text) or ("required" in text) or ("필수" in text) or ("반드시" in text)
        has_loot_hint = any(token in text for token in ["loot", "루팅", "전리품", "파밍"])
        has_loot_intense = any(token in text for token in ["많이", "극대화", "많은", "heavy loot", "loot-heavy", "high loot"]) or (("많" in text or "극대" in text) and has_loot_hint)

        if has_loot_hint:
            directives["loot_ratio"] = 0.30
            directives["loot_min"] = 2
        if has_loot_hint and has_loot_intense:
            directives["loot_ratio"] = 0.40
            directives["loot_min"] = 3

        if has_boss_word and has_skip_word and has_weak_word:
            directives["boss_policy"] = "optional"
            directives["prefer_weak_boss"] = True
        elif has_boss_word and has_required_word:
            directives["boss_policy"] = "required"
        elif any(token in text for token in ["생략하거나", "optional boss", "약하게", "weak boss"]):
            directives["boss_policy"] = "optional"
            if has_weak_word:
                directives["prefer_weak_boss"] = True
        elif has_boss_word and has_skip_word:
            directives["boss_policy"] = "omit"
        elif any(token in text for token in ["보스방은 생략", "보스 생략", "no boss", "without boss"]):
            directives["boss_policy"] = "omit"

        return directives

    def _enforce_room_type_rules(self, rooms):
        nodes_count = len(rooms)
        if nodes_count <= 0:
            return

        directives = self._get_generation_directives()
        loot_ratio = self._safe_float(directives.get("loot_ratio", 0.2), default_value=0.2, min_value=0.0, max_value=1.0)
        loot_min = self._safe_int(directives.get("loot_min", 1), default_value=1, min_value=0, max_value=50)
        boss_policy = str(directives.get("boss_policy", "required")).strip().lower()

        for room in rooms:
            room["RoomType"] = self._sanitize_value(room.get("RoomType"), VALID_ROOM_TYPES, "Combat")

        rooms[0]["RoomType"] = "Start"
        if nodes_count >= 2:
            rooms[-1]["RoomType"] = "Exit"

        interior_indices = list(range(1, nodes_count - 1)) if nodes_count >= 3 else []
        base_fill = "Loot" if loot_ratio >= 0.30 else "Combat"

        for idx in interior_indices:
            if rooms[idx]["RoomType"] in ["Start", "Exit"]:
                rooms[idx]["RoomType"] = base_fill

        boss_indices = [idx for idx in interior_indices if rooms[idx]["RoomType"] == "Boss"]
        if boss_policy == "omit":
            for idx in boss_indices:
                rooms[idx]["RoomType"] = base_fill
            boss_indices = []
        elif boss_policy == "required":
            if len(boss_indices) == 0 and nodes_count >= 3:
                rooms[-2]["RoomType"] = "Boss"
                boss_indices = [nodes_count - 2]
            elif len(boss_indices) > 1:
                keep = max(boss_indices)
                for idx in boss_indices:
                    if idx != keep:
                        rooms[idx]["RoomType"] = "Combat"
                boss_indices = [keep]
        else:  # optional
            if len(boss_indices) > 1:
                keep = max(boss_indices)
                for idx in boss_indices:
                    if idx != keep:
                        rooms[idx]["RoomType"] = "Combat"
                boss_indices = [keep]

        interior_count = len(interior_indices)
        desired_loot = min(interior_count, max(loot_min, int(math.ceil(interior_count * loot_ratio))))
        current_loot = [idx for idx in interior_indices if rooms[idx]["RoomType"] == "Loot"]
        if len(current_loot) < desired_loot:
            needed = desired_loot - len(current_loot)
            candidates = [idx for idx in interior_indices if rooms[idx]["RoomType"] in ["Combat", "Normal"]]
            if boss_policy != "required":
                candidates.extend([idx for idx in interior_indices if rooms[idx]["RoomType"] == "Boss"])

            ordered_candidates = sorted(
                list(dict.fromkeys(candidates)),
                key=lambda v: abs(v - (nodes_count / 2.0)),
                reverse=True
            )
            for idx in ordered_candidates[:needed]:
                rooms[idx]["RoomType"] = "Loot"

        rooms[0]["RoomType"] = "Start"
        if nodes_count >= 2:
            rooms[-1]["RoomType"] = "Exit"
        for idx in interior_indices:
            if rooms[idx]["RoomType"] == "Start" or rooms[idx]["RoomType"] == "Exit":
                rooms[idx]["RoomType"] = "Combat"

    def _default_room_type_for_index(self, node_id, total_count):
        if node_id == 0:
            return "Start"
        if total_count >= 2 and node_id == total_count - 1:
            return "Exit"
        if total_count >= 3 and node_id == total_count - 2:
            return "Boss"
        if node_id % 4 == 0:
            return "Loot"
        return "Combat"

    def _enforce_target_room_count(self, rooms):
        if not isinstance(rooms, list):
            raise ValueError("AI room payload type is invalid. Expected list.")

        target_count = self._get_active_target_room_count()
        source_count = len(rooms)

        normalized = []
        for room in rooms:
            if isinstance(room, dict):
                normalized.append(dict(room))

        if len(normalized) == 0:
            normalized = [{}]

        if len(normalized) > target_count:
            normalized = normalized[:target_count]
        elif len(normalized) < target_count:
            seed_env = VALID_ENV_TYPES[0]
            for existing in normalized:
                existing_env = str(existing.get("EnvType", "")).strip()
                if existing_env:
                    seed_env = self._sanitize_value(existing_env, VALID_ENV_TYPES, VALID_ENV_TYPES[0])
                    break

            while len(normalized) < target_count:
                nid = len(normalized)
                role = self._default_room_type_for_index(nid, target_count)
                normalized.append(
                    {
                        "NodeId": nid,
                        "RoomType": role,
                        "EnvType": seed_env,
                        "Theme": seed_env,
                        "RoomSize": "Large" if role in ["Boss", "Loot"] else "Medium",
                    }
                )

        if source_count != target_count:
            self._log(f"[Room Count] AI returned {source_count}, forced to target {target_count}.")
        return normalized

    def _build_tarkov_grid_coords(self, rooms, adjacency):
        self._log("Using Tarkov Style (Strict Grid) Layout Algorithm...")
        depths = {0: 0}; queue = [0]; parents = {0: 0}
        while queue:
            c = queue.pop(0)
            for n in adjacency.get(c, []):
                if n not in depths:
                    depths[n] = depths[c] + 1; parents[n] = c; queue.append(n)
        coords = {0: (0.0, 0.0)}; used_positions = set([(0.0, 0.0)])
        sorted_nodes = sorted(depths.keys(), key=lambda x: depths[x])
        for nid in sorted_nodes:
            if nid == 0: continue
            p = parents[nid]
            px, py = coords[p]
            directions = [(0, TARKOV_GRID_SIZE_UU), (0, -TARKOV_GRID_SIZE_UU), (TARKOV_GRID_SIZE_UU, 0), (-TARKOV_GRID_SIZE_UU, 0)]
            random.shuffle(directions)
            placed = False
            for dx, dy in directions:
                nx, ny = px + dx, py + dy
                if (nx, ny) not in used_positions:
                    coords[nid] = (nx, ny); used_positions.add((nx, ny)); placed = True; break
            if not placed:
                layer = 1
                while not placed and layer < 10:
                    for x in range(-layer, layer+1):
                        for y in range(-layer, layer+1):
                            nx, ny = px + (x * TARKOV_GRID_SIZE_UU), py + (y * TARKOV_GRID_SIZE_UU)
                            if (nx, ny) not in used_positions:
                                coords[nid] = (nx, ny); used_positions.add((nx, ny)); placed = True; break
                        if placed: break
                    layer += 1
        return coords

    # 🔥 [수정] 부채꼴 완전 차단! 층(Depth)마다 360도를 정확히 N등분하여 사방팔방으로 완벽히 분산배치!
    def _build_radial_coords(self, rooms, adjacency):
        self._log("Using Open World (Radial 360) Layout Algorithm...")
        depths = {0: 0}; queue = [0]; parents = {0: 0}
        while queue:
            c = queue.pop(0)
            for n in adjacency.get(c, []):
                if n not in depths:
                    depths[n] = depths[c] + 1; parents[n] = c; queue.append(n)
        
        max_depth = max(depths.values()) if depths else 1
        map_x_uu = self.map_size_x_var.get() * 100.0
        map_y_uu = self.map_size_y_var.get() * 100.0
        max_radius = min(map_x_uu, map_y_uu) / 2.0 * 0.85 

        coords = {0: (0.0, 0.0)}
        room_radius_by_id = {}
        for idx, room in enumerate(rooms):
            node_id = self._safe_int(room.get("NodeId", idx), default_value=idx, min_value=0)
            room_radius_by_id[node_id] = self._estimate_room_radius_uu(room.get("RoomSize", "Medium"))

        used_positions = [(0.0, 0.0, 0)]
        nodes_by_depth = {}
        for nid, d in depths.items():
            if d not in nodes_by_depth: nodes_by_depth[d] = []
            nodes_by_depth[d].append(nid)

        for d in range(1, max_depth + 1):
            nodes = nodes_by_depth.get(d, [])
            num_nodes = len(nodes)
            if num_nodes == 0: continue
            
            # 해당 뎁스(층)의 방 개수만큼 360도를 파이 조각처럼 완벽히 등분!
            angle_step = (2 * math.pi) / num_nodes
            base_angle_offset = random.uniform(0, 2 * math.pi) # 층마다 시작 각도를 비틂
            layer_radius = (d / max_depth) * max_radius

            for i, nid in enumerate(nodes):
                target_angle = base_angle_offset + (i * angle_step)
                placed = False
                for attempt in range(50):
                    # 등분된 각도를 기준으로 살짝만 랜덤을 섞어 빈자리 찾기
                    angle = target_angle + random.uniform(-0.3, 0.3) * attempt
                    r = layer_radius + random.uniform(-1500, 1500) * attempt
                    
                    x = r * math.cos(angle)
                    y = r * math.sin(angle)
                    x = max(-map_x_uu/2 + 2000, min(map_x_uu/2 - 2000, x))
                    y = max(-map_y_uu/2 + 2000, min(map_y_uu/2 - 2000, y))
                    
                    conflict = False
                    current_radius = room_radius_by_id.get(nid, self._estimate_room_radius_uu("Medium"))
                    for ux, uy, used_nid in used_positions:
                        used_radius = room_radius_by_id.get(used_nid, self._estimate_room_radius_uu("Medium"))
                        min_sep = max(MIN_CONNECTED_DIST_UU, current_radius + used_radius + 900.0)
                        if (x - ux)**2 + (y - uy)**2 < min_sep**2:
                            conflict = True; break
                    if not conflict:
                        coords[nid] = (x, y); used_positions.append((x, y, nid)); placed = True; break
                if not placed:
                    current_radius = room_radius_by_id.get(nid, self._estimate_room_radius_uu("Medium"))
                    fallback_sep_scale = 0.85
                    for _ in range(250):
                        angle = random.uniform(0.0, 2.0 * math.pi)
                        r = random.uniform(max(3000.0, layer_radius * 0.75), min(max_radius, layer_radius * 1.25))
                        x = r * math.cos(angle)
                        y = r * math.sin(angle)
                        x = max(-map_x_uu/2 + 2000, min(map_x_uu/2 - 2000, x))
                        y = max(-map_y_uu/2 + 2000, min(map_y_uu/2 - 2000, y))
                        conflict = False
                        for ux, uy, used_nid in used_positions:
                            used_radius = room_radius_by_id.get(used_nid, self._estimate_room_radius_uu("Medium"))
                            min_sep = max(MIN_CONNECTED_DIST_UU * 0.75, (current_radius + used_radius + 900.0) * fallback_sep_scale)
                            if (x - ux)**2 + (y - uy)**2 < min_sep**2:
                                conflict = True; break
                        if not conflict:
                            coords[nid] = (x, y); used_positions.append((x, y, nid)); placed = True; break
                if not placed:
                    coords[nid] = (x, y); used_positions.append((x, y, nid))
        return coords

    def _process_ai_rooms(self, rooms):
        if not rooms:
            raise ValueError("AI가 유효한 room 배열을 만들지 못했습니다.")

        rooms = self._enforce_target_room_count(rooms)
        nodes_count = len(rooms)
        for i, room in enumerate(rooms):
            room["NodeId"] = i

        # RoomType 무결성(Exit 중복 방지/루팅 비중 보정/보스 정책 반영) 보정
        self._enforce_room_type_rules(rooms)

        adjacency = {i: [] for i in range(nodes_count)}
        for i in range(nodes_count - 1):
            adjacency[i].append(i + 1)
            adjacency[i + 1].append(i)
            
        if nodes_count >= 4:
            for _ in range(nodes_count // 3):
                n1 = random.randint(1, nodes_count - 3)
                n2 = n1 + random.randint(1, 2)
                if n2 < nodes_count and n2 not in adjacency[n1] and n1 != n2:
                    adjacency[n1].append(n2)
                if n2 < nodes_count and n1 not in adjacency[n2] and n1 != n2:
                    adjacency[n2].append(n1)

        selected_style = self.layout_style_var.get()
        if "Tarkov" in selected_style:
            coords = self._build_tarkov_grid_coords(rooms, adjacency)
        else:
            coords = self._build_radial_coords(rooms, adjacency)

        role_titles = {
            "Start": "안전 지대 (Safe Zone)",
            "Normal": "일반 구역 (Standard Sector)",
            "Combat": "교전 구역 (Combat Sector)",
            "Loot": "보급 구역 (Supply Area)",
            "Boss": "위험 구역 (Boss Lair)",
            "Exit": "탈출 지점 (Extraction Point)"
        }
        directives = self._get_generation_directives()

        for i, room in enumerate(rooms):
            nid = room.get("NodeId", i)
            room["NodeId"] = nid
            room["Name"] = f"RaidNode_{nid}"
            room["Seed"] = random.randint(10000, 99999)
            room["PosX"] = float(coords.get(nid, (0,0))[0])
            room["PosY"] = float(coords.get(nid, (0,0))[1])
            room["PosZ"] = 0.0
            
            room["Connections"] = ",".join(map(str, adjacency.get(nid, [])))
            rt = self._sanitize_value(room.get("RoomType"), VALID_ROOM_TYPES, "Combat")
            room["RoomType"]     = rt
            env_val              = self._sanitize_value(room.get("EnvType"), VALID_ENV_TYPES, VALID_ENV_TYPES[0])
            room["EnvType"]      = env_val
            raw_theme = str(room.get("Theme", "")).strip()
            room["Theme"]        = raw_theme if raw_theme else env_val
            room["EnemyPreset"]  = self._sanitize_value(room.get("EnemyPreset"), VALID_ENEMY_PRESETS, "None")
            room["LootLevel"]    = self._sanitize_value(room.get("LootLevel"), VALID_LOOT_LEVELS, "Common")
            room["LootStrategy"] = self._sanitize_value(room.get("LootStrategy"), VALID_LOOT_STRAT, "Scattered")
            room["BotProfile"]   = self._sanitize_value(room.get("BotProfile"), VALID_BOT_PROFILES, "Tactical")
            raw_prefab = str(room.get("RoomPrefabId", "")).strip()
            room["RoomPrefabId"] = raw_prefab if raw_prefab else rt

            room["RoomSize"] = self._normalize_room_size(room.get("RoomSize", "Medium"))
            room["ZoneId"] = self._safe_int(room.get("ZoneId", 1), default_value=1, min_value=1, max_value=99)
            room["Difficulty"] = self._normalize_difficulty(room.get("Difficulty", self.difficulty_var.get()))
            room["CombatWeight"] = self._safe_float(room.get("CombatWeight", 1.0), default_value=1.0, min_value=0.1, max_value=3.0)
            room["ObstacleDensity"] = self._safe_float(room.get("ObstacleDensity", 0.5), default_value=0.5, min_value=0.0, max_value=1.0)
            room["EnterableBuildingRatio"] = self._safe_float(room.get("EnterableBuildingRatio", 0.45), default_value=0.45, min_value=0.0, max_value=1.0)
            room["SpawnCount"] = self._safe_int(room.get("SpawnCount", 3), default_value=3, min_value=0, max_value=64)
            room["LootCount"] = self._safe_int(room.get("LootCount", 3), default_value=3, min_value=0, max_value=64)
            if rt == "Boss" and directives.get("prefer_weak_boss", False):
                room["Difficulty"] = min(room["Difficulty"], 1.1)
                room["CombatWeight"] = min(room["CombatWeight"], 1.0)
                if room["EnemyPreset"] in ["BossGuard", "None"]:
                    room["EnemyPreset"] = "Raider"

            title_text = role_titles.get(rt, "미확인 구역 (Unknown Sector)")
            existing_tags = str(room.get("NodeTags", "")).strip()
            tag_tokens = [tok.strip() for tok in existing_tags.split(",") if tok.strip()]
            # Banner title is always NodeTags[0]. Existing tags are preserved as metadata.
            dedup_tail = [tok for tok in tag_tokens if tok != title_text]
            room["NodeTags"] = ", ".join([title_text] + dedup_tail) if dedup_tail else title_text
            existing_role = str(room.get("RoomRole", "")).strip()
            room["RoomRole"] = self._sanitize_value(existing_role, VALID_ROOM_TYPES, rt)

            room["TraversalLaneSeeds"] = self._safe_int(room.get("TraversalLaneSeeds", 2), default_value=random.randint(2, 4), min_value=1, max_value=8)
            
        self._save_to_csv(rooms)

    def _run_ollama_generation(self, model_name, user_prompt):
        self._log(f"Connecting to Local Ollama ({model_name})...")
        url = "http://127.0.0.1:11434/api/chat"
        payload = {"model": model_name, "messages": [{"role": "system", "content": self._get_system_prompt(is_ai=True)}, {"role": "user", "content": user_prompt}], "stream": False, "format": "json", "options": {"temperature": 0.7}}
        try:
            req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(req) as response:
                resp_dict = json.loads(response.read().decode('utf-8'))
                full_response = resp_dict.get("message", {}).get("content", "")
            safe_log = full_response[:150].replace('\n', ' ')
            self._log(f"[AI Raw Output] {safe_log}...")
            if not full_response.strip(): raise ValueError("AI가 빈 문자열을 반환했습니다.")
            level_data = self._parse_level_data_from_ai_output(full_response)
            rooms = level_data.get("rooms", [])
            if not rooms: raise ValueError("Ollama가 방 데이터를 반환하지 않았습니다.")
            self._process_ai_rooms(rooms)
            self.after(0, lambda: self._generation_complete(True, "Ollama Generation Success!"))
        except Exception as e:
            error_msg = str(e) 
            self.after(0, lambda: self._generation_complete(False, error_msg))

    def _run_openai_generation(self, api_key, user_prompt):
        self._log("Connecting to OpenAI Cloud API...")
        openai.api_key = api_key
        try:
            response = openai.chat.completions.create(model="gpt-4o", response_format={"type": "json_object"}, messages=[{"role": "system", "content": self._get_system_prompt(is_ai=True)}, {"role": "user", "content": user_prompt}], temperature=0.7)
            raw_content = response.choices[0].message.content
            safe_log = raw_content[:150].replace('\n', ' ')
            self._log(f"[AI Raw Output] {safe_log}...")
            if not raw_content.strip(): raise ValueError("OpenAI가 빈 문자열을 반환했습니다.")
            level_data = self._parse_level_data_from_ai_output(raw_content)
            rooms = level_data.get("rooms", [])
            if not rooms: raise ValueError("OpenAI가 방 데이터를 반환하지 않았습니다.")
            self._process_ai_rooms(rooms)
            self.after(0, lambda: self._generation_complete(True, "OpenAI Generation Success!"))
        except Exception as e:
            error_msg = str(e) 
            self.after(0, lambda: self._generation_complete(False, error_msg))
        
    def _run_local_generation(self):
        self._log("Running Offline Algorithm...")
        try:
            nodes = self._get_active_target_room_count()
            base = []
            for nid in range(nodes):
                role = "Start" if nid == 0 else "Exit" if nid == nodes-1 else "Boss" if nid == nodes-2 else "Loot" if nid%4==0 else "Combat"
                base.append({ "NodeId": nid, "RoomType": role, "EnvType": random.choice(VALID_ENV_TYPES), "RoomSize": "Large" if role in ["Boss","Loot"] else "Medium" })
            self._process_ai_rooms(base)
            self.after(0, lambda: self._generation_complete(True, "Offline Gen Success!"))
        except Exception as e: 
            error_msg = str(e) 
            self.after(0, lambda: self._generation_complete(False, error_msg))

    def _save_to_csv(self, rooms):
        f_path = os.path.join(self.out_dir_var.get(), "DT_AI_Raid_Design.csv")
        self._log(f"Saving Data to CSV: {f_path}")
        with open(f_path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction='ignore')
            w.writeheader()
            w.writerows(rooms)

    def _generation_complete(self, success, message):
        self.btn_generate.config(text="GENERATE CSV", bg=self.accent_color, state="normal")
        if success:
            self._log("★ ALL TASKS COMPLETED SUCCESSFULLY ★")
            messagebox.showinfo("Success", message)
        else:
            self._log(f"✖ ERROR: {message}")
            messagebox.showerror("Error", message)

if __name__ == "__main__": 
    MasterArchitect().mainloop()
