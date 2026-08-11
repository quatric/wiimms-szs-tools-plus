#include "lib-model-dae.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ExportModelToDAE(const model_t *model, const char *out_xml_file) {
    if (!model || !out_xml_file) return -1;
    
    FILE *f = fopen(out_xml_file, "w");
    if (!f) return -1;
    
    fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(f, "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n");
    fprintf(f, "  <asset>\n");
    fprintf(f, "    <contributor>\n");
    fprintf(f, "      <authoring_tool>wiimms-szs-tools-nintendo exporter</authoring_tool>\n");
    fprintf(f, "    </contributor>\n");
    fprintf(f, "    <created>2026-08-10T22:12:22Z</created>\n");
    fprintf(f, "    <modified>2026-08-10T22:12:22Z</modified>\n");
    fprintf(f, "    <unit name=\"meter\" meter=\"1\"/>\n");
    fprintf(f, "    <up_axis>Y_UP</up_axis>\n");
    fprintf(f, "  </asset>\n");

    fprintf(f, "  <library_geometries>\n");
    for (size_t i = 0; i < model->num_meshes; i++) {
        const mesh_t *mesh = &model->meshes[i];
        fprintf(f, "    <geometry id=\"mesh_%zu-mesh\" name=\"%s\">\n", i, mesh->name);
        fprintf(f, "      <mesh>\n");
        
        // Positions
        fprintf(f, "        <source id=\"mesh_%zu-positions\">\n", i);
        fprintf(f, "          <float_array id=\"mesh_%zu-positions-array\" count=\"%zu\">", i, mesh->num_positions * 3);
        for (size_t j = 0; j < mesh->num_positions; j++) {
            fprintf(f, "%f %f %f ", mesh->positions[j].x, mesh->positions[j].y, mesh->positions[j].z);
        }
        fprintf(f, "</float_array>\n");
        fprintf(f, "          <technique_common>\n");
        fprintf(f, "            <accessor source=\"#mesh_%zu-positions-array\" count=\"%zu\" stride=\"3\">\n", i, mesh->num_positions);
        fprintf(f, "              <param name=\"X\" type=\"float\"/>\n");
        fprintf(f, "              <param name=\"Y\" type=\"float\"/>\n");
        fprintf(f, "              <param name=\"Z\" type=\"float\"/>\n");
        fprintf(f, "            </accessor>\n");
        fprintf(f, "          </technique_common>\n");
        fprintf(f, "        </source>\n");
        
        // Normals
        fprintf(f, "        <source id=\"mesh_%zu-normals\">\n", i);
        fprintf(f, "          <float_array id=\"mesh_%zu-normals-array\" count=\"%zu\">", i, mesh->num_normals * 3);
        for (size_t j = 0; j < mesh->num_normals; j++) {
            fprintf(f, "%f %f %f ", mesh->normals[j].x, mesh->normals[j].y, mesh->normals[j].z);
        }
        fprintf(f, "</float_array>\n");
        fprintf(f, "          <technique_common>\n");
        fprintf(f, "            <accessor source=\"#mesh_%zu-normals-array\" count=\"%zu\" stride=\"3\">\n", i, mesh->num_normals);
        fprintf(f, "              <param name=\"X\" type=\"float\"/>\n");
        fprintf(f, "              <param name=\"Y\" type=\"float\"/>\n");
        fprintf(f, "              <param name=\"Z\" type=\"float\"/>\n");
        fprintf(f, "            </accessor>\n");
        fprintf(f, "          </technique_common>\n");
        fprintf(f, "        </source>\n");
        
        // Texcoords
        fprintf(f, "        <source id=\"mesh_%zu-texcoords\">\n", i);
        fprintf(f, "          <float_array id=\"mesh_%zu-texcoords-array\" count=\"%zu\">", i, mesh->num_texcoords * 2);
        for (size_t j = 0; j < mesh->num_texcoords; j++) {
            fprintf(f, "%f %f ", mesh->texcoords[j].u, mesh->texcoords[j].v);
        }
        fprintf(f, "</float_array>\n");
        fprintf(f, "          <technique_common>\n");
        fprintf(f, "            <accessor source=\"#mesh_%zu-texcoords-array\" count=\"%zu\" stride=\"2\">\n", i, mesh->num_texcoords);
        fprintf(f, "              <param name=\"S\" type=\"float\"/>\n");
        fprintf(f, "              <param name=\"T\" type=\"float\"/>\n");
        fprintf(f, "            </accessor>\n");
        fprintf(f, "          </technique_common>\n");
        fprintf(f, "        </source>\n");
        
        // Vertices
        fprintf(f, "        <vertices id=\"mesh_%zu-vertices\">\n", i);
        fprintf(f, "          <input semantic=\"POSITION\" source=\"#mesh_%zu-positions\"/>\n", i);
        fprintf(f, "        </vertices>\n");
        
        // Triangles
        fprintf(f, "        <triangles count=\"%zu\">\n", mesh->num_vertices / 3);
        fprintf(f, "          <input semantic=\"VERTEX\" source=\"#mesh_%zu-vertices\" offset=\"0\"/>\n", i);
        fprintf(f, "          <input semantic=\"NORMAL\" source=\"#mesh_%zu-normals\" offset=\"1\"/>\n", i);
        fprintf(f, "          <input semantic=\"TEXCOORD\" source=\"#mesh_%zu-texcoords\" offset=\"2\" set=\"0\"/>\n", i);
        fprintf(f, "          <p>");
        for (size_t j = 0; j < mesh->num_vertices; j++) {
            fprintf(f, "%d %d %d ", mesh->vertices[j].position_idx, mesh->vertices[j].normal_idx, mesh->vertices[j].texcoord_idx);
        }
        fprintf(f, "</p>\n");
        fprintf(f, "        </triangles>\n");
        
        fprintf(f, "      </mesh>\n");
        fprintf(f, "    </geometry>\n");
    }
    fprintf(f, "  </library_geometries>\n");

    fprintf(f, "  <library_visual_scenes>\n");
    fprintf(f, "    <visual_scene id=\"Scene\" name=\"Scene\">\n");
    
    // Basic flat joint output for skeleton
    for (size_t i = 0; i < model->num_joints; i++) {
        const joint_t *joint = &model->joints[i];
        if (joint->parent_idx == -1) { // Only writing roots here for simplicity
            fprintf(f, "      <node id=\"%s\" name=\"%s\" type=\"JOINT\">\n", joint->name, joint->name);
            fprintf(f, "        <translate sid=\"translate\">%f %f %f</translate>\n", joint->translate.x, joint->translate.y, joint->translate.z);
            fprintf(f, "        <rotate sid=\"rotateX\">1 0 0 %f</rotate>\n", joint->rotate.x);
            fprintf(f, "        <rotate sid=\"rotateY\">0 1 0 %f</rotate>\n", joint->rotate.y);
            fprintf(f, "        <rotate sid=\"rotateZ\">0 0 1 %f</rotate>\n", joint->rotate.z);
            fprintf(f, "        <scale sid=\"scale\">%f %f %f</scale>\n", joint->scale.x, joint->scale.y, joint->scale.z);
            fprintf(f, "      </node>\n");
        }
    }
    
    for (size_t i = 0; i < model->num_meshes; i++) {
        fprintf(f, "      <node id=\"Node_mesh_%zu\" name=\"%s\">\n", i, model->meshes[i].name);
        fprintf(f, "        <instance_geometry url=\"#mesh_%zu-mesh\"/>\n", i);
        fprintf(f, "      </node>\n");
    }
    
    fprintf(f, "    </visual_scene>\n");
    fprintf(f, "  </library_visual_scenes>\n");
    
    fprintf(f, "  <scene>\n");
    fprintf(f, "    <instance_visual_scene url=\"#Scene\"/>\n");
    fprintf(f, "  </scene>\n");
    
    fprintf(f, "</COLLADA>\n");
    
    fclose(f);
    return 0;
}
