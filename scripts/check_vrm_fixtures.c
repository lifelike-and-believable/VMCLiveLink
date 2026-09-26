/*
 * Validates VRM/glTF fixtures with the same cgltf the importer bundles, and dumps what glTF
 * semantics say the importer must produce, as JSON on stdout:
 *   - world position (bind pose, glTF space) of every joint node
 *   - for every vertex of every mesh node: dominant joint node index and rest-pose world position
 *     (rigid: node world transform; skinned: weighted joint matrices, joint world x inverse bind)
 *
 * scripts/check_vrm_fixtures.py compiles this and compares the output with the *.expected.json
 * sidecars written by make_vrm_fixtures.py.
 *
 * Build: cc -O1 -I Plugins/VRMInterchange/ThirdParty/cgltf scripts/check_vrm_fixtures.c -lm
 * Usage: check_vrm_fixtures <file.vrm>
 */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <stdio.h>

static void mat_mul_point(const float m[16], const float p[3], float out[3])
{
	/* cgltf matrices are column-major */
	for (int r = 0; r < 3; ++r)
	{
		out[r] = m[0 * 4 + r] * p[0] + m[1 * 4 + r] * p[1] + m[2 * 4 + r] * p[2] + m[3 * 4 + r];
	}
}

static void mat_mul(const float a[16], const float b[16], float out[16])
{
	/* column-major: out = a * b */
	float r[16];
	for (int c = 0; c < 4; ++c)
	{
		for (int row = 0; row < 4; ++row)
		{
			float s = 0.f;
			for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = s;
		}
	}
	for (int i = 0; i < 16; ++i) out[i] = r[i];
}

static void joint_matrix(const cgltf_skin* skin, cgltf_size joint, float out[16])
{
	float world[16], ibm[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	cgltf_node_transform_world(skin->joints[joint], world);
	if (skin->inverse_bind_matrices) cgltf_accessor_read_float(skin->inverse_bind_matrices, joint, ibm, 16);
	mat_mul(world, ibm, out);
}

static const cgltf_node* nearest_joint_ancestor(const cgltf_data* data, const cgltf_node* node)
{
	for (const cgltf_node* p = node->parent; p; p = p->parent)
	{
		for (cgltf_size s = 0; s < data->skins_count; ++s)
		{
			for (cgltf_size j = 0; j < data->skins[s].joints_count; ++j)
			{
				if (data->skins[s].joints[j] == p)
				{
					return p;
				}
			}
		}
	}
	return NULL;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s file.vrm\n", argv[0]);
		return 2;
	}

	cgltf_options options = {0};
	cgltf_data* data = NULL;
	if (cgltf_parse_file(&options, argv[1], &data) != cgltf_result_success)
	{
		fprintf(stderr, "parse failed\n");
		return 1;
	}
	if (cgltf_load_buffers(&options, data, argv[1]) != cgltf_result_success)
	{
		fprintf(stderr, "load_buffers failed\n");
		cgltf_free(data);
		return 1;
	}
	cgltf_result v = cgltf_validate(data);
	if (v != cgltf_result_success)
	{
		fprintf(stderr, "validate failed: %d\n", (int)v);
		cgltf_free(data);
		return 1;
	}

	printf("{\"joints\":{");
	int first = 1;
	for (cgltf_size s = 0; s < data->skins_count; ++s)
	{
		for (cgltf_size j = 0; j < data->skins[s].joints_count; ++j)
		{
			const cgltf_node* n = data->skins[s].joints[j];
			float m[16];
			cgltf_node_transform_world(n, m);
			printf("%s\"%d\":[%.6f,%.6f,%.6f]", first ? "" : ",", (int)(n - data->nodes), m[12], m[13], m[14]);
			first = 0;
		}
	}
	printf("},\"vertices\":[");

	first = 1;
	for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
	{
		const cgltf_node* node = &data->nodes[ni];
		if (!node->mesh)
		{
			continue;
		}
		float world[16];
		cgltf_node_transform_world(node, world);
		const cgltf_node* rigid_bone = node->skin ? NULL : nearest_joint_ancestor(data, node);

		int vertex = 0;
		for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
		{
			const cgltf_primitive* prim = &node->mesh->primitives[pi];
			const cgltf_accessor *pos = NULL, *jnt = NULL, *wgt = NULL;
			for (cgltf_size a = 0; a < prim->attributes_count; ++a)
			{
				const cgltf_attribute* attr = &prim->attributes[a];
				if (attr->type == cgltf_attribute_type_position) pos = attr->data;
				if (attr->type == cgltf_attribute_type_joints && attr->index == 0) jnt = attr->data;
				if (attr->type == cgltf_attribute_type_weights && attr->index == 0) wgt = attr->data;
			}
			for (cgltf_size i = 0; i < pos->count; ++i, ++vertex)
			{
				float p[3], out[3];
				cgltf_accessor_read_float(pos, i, p, 3);
				int bone_node = -1;
				int legacy_bone_node = -1;
				if (node->skin && jnt && wgt)
				{
					/* JOINTS_0 values index into this node's skin.joints, not into nodes. */
					cgltf_uint joints[4];
					float weights[4];
					cgltf_accessor_read_uint(jnt, i, joints, 4);
					cgltf_accessor_read_float(wgt, i, weights, 4);
					int best = 0;
					for (int k = 1; k < 4; ++k) if (weights[k] > weights[best]) best = k;
					bone_node = (int)(node->skin->joints[joints[best]] - data->nodes);

					/* What VRMTranslator.cpp (as of d1c7c20) computes: only skins[0] is used, and each
					 * JOINTS_0 value is looked up as a *node* index in skins[0]'s node->joint map,
					 * falling back to joint 0. Reported to show the impact of findings T-01/T-02. */
					const cgltf_skin* skin0 = &data->skins[0];
					int legacy_joint = 0;
					for (cgltf_size k = 0; k < skin0->joints_count; ++k)
					{
						if ((cgltf_uint)(skin0->joints[k] - data->nodes) == joints[best]) { legacy_joint = (int)k; break; }
					}
					legacy_bone_node = (int)(skin0->joints[legacy_joint] - data->nodes);
					out[0] = out[1] = out[2] = 0.f;
					for (int k = 0; k < 4; ++k)
					{
						float jm[16], q[3];
						joint_matrix(node->skin, joints[k], jm);
						mat_mul_point(jm, p, q);
						for (int r = 0; r < 3; ++r) out[r] += weights[k] * q[r];
					}
				}
				else
				{
					bone_node = rigid_bone ? (int)(rigid_bone - data->nodes) : -1;
					/* The current importer binds rigid meshes to joint 0 of skins[0]. */
					legacy_bone_node = data->skins_count ? (int)(data->skins[0].joints[0] - data->nodes) : -1;
					mat_mul_point(world, p, out);
				}
				printf("%s{\"mesh_node\":\"%s\",\"vertex\":%d,\"bone_node\":%d,\"legacy_bone_node\":%d,\"pos\":[%.6f,%.6f,%.6f]}",
					first ? "" : ",", node->name ? node->name : "", vertex, bone_node, legacy_bone_node, out[0], out[1], out[2]);
				first = 0;
			}
		}
	}
	printf("]}\n");

	cgltf_free(data);
	return 0;
}
