#pragma once
#include "tr_local.h"

// Stock skin regions in texture coordinates. Bounds exclude mixed atlas regions.
static void R_InitSkinProfile(shader_t &shader)
{
	struct Profile { const char *model, *materials; vec4_t bounds; };
	static const Profile profiles[] = {
		{"alora", "|alora_hands|", {0,0,1,1}},
		{"alora2", "|alora2_face|", {0,0.28f,1,1}},
		{"alora2", "|alora2_forehead|", {0,0.3f,1,1}},
		{"alora2", "|alora2_tentacles|alora2_tentacles_blue|alora2_tentacles_red|", {0,0,1,0.45f}},
		{"alora2", "|alora2_torso_arms|", {0.05f,0.12f,0.65f,0.72f}},
		{"alora2", "|alora2_torso_hands|", {0,0.45f,1,1}},
		{"bespin_cop", "|face|hand|", {0,0,1,1}},
		{"chiss", "|base_test|basic_hand|", {0,0,1,1}},
		{"cultist", "|face|face_b|face_blue|face_red|", {0.23f,0.25f,0.77f,0.4f}},
		{"cultist", "|hands|", {0,0,0.6f,1}},
		{"desann", "|face|hand_right|", {0,0,1,1}},
		{"galak", "|face|", {0,0,1,1}},
		{"gran", "|head|basic_hand|", {0,0,1,1}},
		{"human_merc", "|human_merc_face|", {0.18f,0.5f,0.78f,0.8f}},
		{"human_merc", "|racto_face|", {0,0,1,1}},
		{"imperial", "|face|bl_face|br_face|", {0,0,1,1}},
		{"imperial_worker", "|head|", {0,0.65f,1,1}},
		{"jan", "|face|", {0,0,1,1}},
		{"jan", "|l_hand|", {0.3f,0.15f,0.8f,1}},
		{"jedi", "|face_01|face_02|face_master|basic_hand|j2_basic_hand|", {0,0,1,1}},
		{"jedi_hf", "|face|face_a|face_b|", {0,0.25f,1,1}},
		{"jedi_hf", "|hands_01|hands_02|hands_03|", {0.35f,0.2f,0.82f,1}},
		{"jedi_hf", "|arms_05|", {0,0,0.45f,1}},
		{"jedi_hf", "|arms_06|", {0,0,1,1}},
		{"jedi_hm", "|face|face2|face_blonde|", {0,0.18f,1,1}},
		{"jedi_hm", "|hands2|robes02_hands|", {0,0,1,1}},
		{"jedi_kdm", "|head_1|head_2|head_3|", {0.42f,0,0.69f,1}},
		{"jedi_kdm", "|hands_1|neck_1|", {0,0,1,1}},
		{"jedi_kdm", "|arms_2|", {0.2f,0.15f,0.7f,0.8f}},
		{"jedi_rm", "|head01|head02|head03|hand|", {0,0,1,1}},
		{"jedi_rm", "|arms_02|arms_03|arms_05|", {0.1f,0.55f,0.8f,1}},
		{"jedi_tf", "|face|face_01|face_02|face_03|head|head_01|head_02|head_03|tentacles|tentacles_01|tentacles_02|tentacles_03|torso_01_arms|torso_02_arms|torso_01_hands|torso_01_skin|torso_02_skin|torso_03_skin|", {0,0,1,1}},
		{"jedi_zf", "|face_01|face_02|face_03|", {0,0.25f,1,1}},
		{"jedi_zf", "|arms_01|arms_03|arms_04|", {0,0,1,1}},
		{"jedi_zf", "|arms_02|", {0.72f,0,1,1}},
		{"jedi_zf", "|arms_05|", {0,0,1,0.75f}},
		{"jedi_zf", "|arms_06|", {0,0.35f,1,1}},
		{"jedi_zf", "|hands|hands_03|hands_04|hands_05|hands_06|", {0,0,1,1}},
		{"jedi_zf", "|hands_02|", {0.7f,0,1,1}},
		{"jeditrainer", "|trainer_face|", {0,0,1,1}},
		{"kyle", "|kyle_face|", {0,0,1,1}},
		{"lando", "|face|basic_hand|", {0,0,1,1}},
		{"luke", "|face|luke_face_new|", {0,0,1,1}},
		{"monmothma", "|face|", {0,0,1,1}},
		{"monmothma", "|basic_hand|l_hand|", {0.25f,0.25f,0.75f,1}},
		{"morgan", "|face|basic_hand|", {0,0,1,1}},
		{"noghri", "|head|", {0,0,1,1}},
		{"prisoner", "|face_01|face_02|basic_hand|", {0,0,1,1}},
		{"rax_joris", "|face|", {0,0.25f,1,1}},
		{"rax_joris", "|hands|", {0,0,1,1}},
		{"rebel", "|face_01|basic_hand|", {0,0,1,1}},
		{"rebel_pilot", "|face|", {0,0,1,1}},
		{"reborn", "|face|boss_face|forc_face|", {0,0,1,1}},
		{"reborn_new", "|head_face|", {0.2f,0.3f,0.7f,0.46f}},
		{"reborn_twin", "|head|head_boss|head_blue|head_red|", {0,0.12f,0.32f,1}},
		{"rodian", "|head|basic_hand|", {0,0,1,1}},
		{"rosh_penin", "|face|", {0,0.23f,1,1}},
		{"rosh_penin", "|hands|", {0,0,1,0.75f}},
		{"saboteur", "|saboteur_face|", {0,0.25f,1,1}},
		{"tavion", "|tavion_face|", {0,0,1,1}},
		{"tavion", "|l_hand|", {0.3f,0.15f,0.8f,1}},
		{"tavion_new", "|face|face_glow|", {0,0.25f,1,1}},
		{"tavion_new", "|hands|hands_glow|", {0,0,1,1}},
		{"trandoshan", "|head|hand|foot|", {0,0,1,1}},
		{"ugnaught", "|head|", {0,0,1,1}},
		{"weequay", "|head|l_hand|", {0,0,1,1}},
	};
	char name[MAX_QPATH], material[MAX_QPATH + 2];
	COM_StripExtension(shader.name, name, sizeof(name));
	Q_strlwr(name);
	if (Q_strncmp(name, "models/players/", 15)) return;
	char *slash = strchr(name + 15, '/');
	if (!slash) return;
	*slash = '\0';
	Com_sprintf(material, sizeof(material), "|%s|", slash + 1);
	for (const auto &profile : profiles)
		if (!strcmp(name + 15, profile.model) && strstr(profile.materials, material))
		{
			VectorCopy4(profile.bounds, shader.skinBounds);
			return;
		}
}
