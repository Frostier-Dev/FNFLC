import json
import struct
import os
import sys

LANG = {
    "en": {
        "welcome": "=== FNF CHART CONVERTER TO B-SLICE ===",
        "folder_created": "Folder '{}' created automatically.",
        "ask_file": "Enter the JSON file name (e.g., test-chart): ",
        "err_not_found": "\nError: File '{}' was not found inside '{}' folder!",
        "err_invalid_json": "\nError: File is not a valid JSON!",
        "err_no_notes": "\nError: No structured difficulties or notes found inside this JSON.",
        "loaded": "\nFile loaded from '{}/'!",
        "found_diffs": "Difficulties found: {}",
        "converting": "Converting and saving to '{}/'...\n",
        "pico_skip": "Pico Mix detected ('{}'), skipping for now...",
        "success_note": "  -> Generated successfully: {}\n     [Opponent Notes: {} | Player Notes: {} | Total: {}]\n     [File Size: {:.2f} KB]",
        "warn_events": "     ⚠️ Warning: {} events left behind (events are not baked into B-Slice charts).",
        "warn_skipped": "     ❌ Warning: {} notes skipped or corrupted (invalid lane value > 7).",
        "done": "\nFinished! {} .fnfbin files saved inside '{}' folder.",
        "ask_continue": "\nDo you want to convert another file? (y/n): ",
        "bye": "\nPress Enter to exit..."
    },
    "pt": {
        "welcome": "=== CONVERSOR DE CHARTS FNF PARA B-SLICE ===",
        "folder_created": "Pasta '{}' criada automaticamente.",
        "ask_file": "Digite o nome do arquivo JSON (ex: test-chart): ",
        "err_not_found": "\nErro: O arquivo '{}' nao foi encontrado dentro da pasta '{}/'!",
        "err_invalid_json": "\nErro: O arquivo nao e um JSON valido!",
        "err_no_notes": "\nErro: Nenhuma dificuldade ou nota estruturada foi encontrada dentro deste JSON.",
        "loaded": "\nArquivo carregado de '{}/'!",
        "found_diffs": "Dificuldades encontradas: {}",
        "converting": "Convertendo e salvando em '{}/'...\n",
        "pico_skip": "Pico Mix detectado ('{}'), pulando por enquanto...",
        "success_note": "  -> Gerado com sucesso: {}\n     [Notas do Oponente: {} | Notas do Player: {} | Total: {}]\n     [Tamanho do Arquivo: {:.2f} KB]",
        "warn_events": "     ⚠️ Aviso: {} eventos deixados para tras (eventos nao sao embutidos nas charts B-Slice).",
        "warn_skipped": "     ❌ Aviso: {} notas ignoradas ou corrompidas (pista com valor invalido > 7).",
        "done": "\nConcluido! {} arquivos salvos na pasta '{}'.",
        "ask_continue": "\nDeseja converter outro arquivo? (s/n): ",
        "bye": "\nPressione Enter para sair..."
    }
}

def extract_difficulty_to_bin(data, target_difficulty, output_bin_path, t):
    speed_data = data.get("scrollSpeed", 1.0)
    if isinstance(speed_data, dict):
        scroll_speed = float(speed_data.get(target_difficulty, speed_data.get("default", 1.0)))
    else:
        scroll_speed = float(speed_data)
        
    bpm = float(data.get("bpm", 100.0))
    
    all_difficulties = data.get("notes", {})
    difficulty_notes = all_difficulties.get(target_difficulty, [])
    
    if not difficulty_notes or not isinstance(difficulty_notes, list):
        return False

    # Dicionário para agrupar notas que acontecem no exato mesmo milissegundo
    grouped_time_slots = {}
    skipped_count = 0
    
    for note in difficulty_notes:
        if isinstance(note, dict):
            try:
                time_ms = int(round(float(note.get("t", note.get("time", 0)))))
                lane = int(note.get("d", note.get("data", note.get("lane", 0))))
                hold_ms = int(round(float(note.get("l", note.get("length", note.get("sustain", 0))))))
                
                if 0 <= lane <= 7:
                    if time_ms not in grouped_time_slots:
                        grouped_time_slots[time_ms] = []
                    grouped_time_slots[time_ms].append((lane, hold_ms))
                else:
                    skipped_count += 1
            except Exception:
                skipped_count += 1

    raw_notes = []
    opponent_count = 0
    player_count = 0

    # Processa os blocos de tempo ordenados cronologicamente
    for time_ms in sorted(grouped_time_slots.keys()):
        slots = grouped_time_slots[time_ms]
        
        # Para o Nintendo DS não engasgar com múltiplas structs no mesmo milissegundo,
        # nós unificamos notas duplas usando uma bitmask de direções (00000000)
        # No entanto, para manter a compatibilidade com a struct BSliceNote simples de 7 bytes:
        for lane, hold_ms in slots:
            raw_notes.append((time_ms, lane, hold_ms))
            if lane <= 3:
                opponent_count += 1
            else:
                player_count += 1

    if raw_notes:
        total_notes = len(raw_notes)
        
        with open(output_bin_path, 'wb') as bin_file:
            magic_bytes = b"FNFB"
            header_pack = struct.pack("<4sHff", magic_bytes, total_notes, bpm, scroll_speed)
            bin_file.write(header_pack)
            
            for time_ms, lane, hold_ms in raw_notes:
                if hold_ms > 65535: hold_ms = 65535
                note_pack = struct.pack("<IBH", time_ms, lane, hold_ms)
                bin_file.write(note_pack)
                
        file_size = os.path.getsize(output_bin_path) / 1024
        print(t["success_note"].format(os.path.basename(output_bin_path), opponent_count, player_count, total_notes, file_size))
        
        if skipped_count > 0:
            print(t["warn_skipped"].format(skipped_count))
            
        return True
    return False

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    lang_choice = input("Select Language / Selecione o Idioma (en/pt) [Default: en]: ").strip().lower()
    current_lang = "pt" if lang_choice in ["pt", "br", "portugues"] else "en"
    
    t = LANG[current_lang]
    print("\n" + t["welcome"])
    
    input_folder = os.path.join(script_dir, "charts")
    output_folder = os.path.join(script_dir, "output")
    
    if not os.path.exists(input_folder):
        os.makedirs(input_folder)
    
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)
        print(t["folder_created"].format("output"))

    while True:
        print("-" * 40)
        json_name = input(t["ask_file"]).strip()
        
        if not json_name:
            continue
            
        if not json_name.endswith(".json"):
            json_name += ".json"
            
        full_input_path = os.path.join(input_folder, json_name)
            
        if not os.path.exists(full_input_path):
            print(t["err_not_found"].format(json_name, "charts"))
        else:
            with open(full_input_path, 'r', encoding='utf-8') as f:
                try:
                    chart_data = json.load(f)
                except json.JSONDecodeError:
                    print(t["err_invalid_json"])
                    continue
            
            if "song" in chart_data and isinstance(chart_data["song"], dict):
                chart_data = chart_data["song"]
                
            all_diffs = chart_data.get("notes", {})
            available_diffs = list(all_diffs.keys()) if isinstance(all_diffs, dict) else []
            
            if not available_diffs:
                print(t["err_no_notes"])
            else:
                print(t["loaded"].format("charts"))
                print(t["found_diffs"].format(available_diffs))
                print(t["converting"].format("output"))
                
                base_name = os.path.splitext(json_name)[0]
                conversoes_sucesso = 0
                
                events_list = chart_data.get("events", [])
                events_count = len(events_list)
                
                for diff in available_diffs:
                    if "pico" in diff.lower():
                        print(t["pico_skip"].format(diff))
                        continue
                    
                    output_name = f"{base_name}-{diff.lower()}.fnfbin"
                    full_output_path = os.path.join(output_folder, output_name)
                    
                    if extract_difficulty_to_bin(chart_data, diff, full_output_path, t):
                        conversoes_sucesso += 1
                        if events_count > 0:
                            print(t["warn_events"].format(events_count))
                        print("")
                        
                print(t["done"].format(conversoes_sucesso, "output"))
        
        choice = input(t["ask_continue"]).strip().lower()
        if current_lang == "pt" and choice not in ["s", "sim", "y", "yes"]:
            break
        elif current_lang == "en" and choice not in ["y", "yes"]:
            break

    input(t["bye"])

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"\nError:\n{e}")
        input("\nPress Enter to close...")