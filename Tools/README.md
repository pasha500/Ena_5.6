# Tools Overview (MECE)

이 폴더는 역할 기준으로 다음 3가지로 구분한다.

1. **Production Generator**
- `MasterArchitect_Patched.py`
- 목적: AI/오프라인 기반 `DT_AI_Raid_Design.csv` 생성
- 운영 기준: 실제 레벨 생성 파이프라인에서 사용

2. **Data/Validation**
- `DT_AI_Raid_Design.csv`
- 목적: 생성 결과 샘플/검증용 CSV

3. **Diagnostics & One-off Utilities**
- `debug_loot_asset_types.py`
- `debug_loot_runtime.py`
- `find_medical_heal_values.py`
- `inspect_als_base_characterbp_refs.py`
- `inspect_medical_blueprint.py`
- `inspect_medical_bp_graph.py`
- `inspect_wave_profile_stages.py`
- `set_wave_profile_stage_defaults.py`
- `wave_profile_inspect_output.txt`
- 목적: 특정 이슈 추적/분석용 (상시 런타임 의존 없음)

## Rules

- 운영 파이프라인은 `MasterArchitect_Patched.py` 하나를 단일 진입점으로 유지한다.
- 진단 스크립트는 런타임 코드(`Source`, `Plugins`)와 직접 결합하지 않는다.
- 진단 산출물(txt/log)은 필요 시 재생성 가능해야 한다.
