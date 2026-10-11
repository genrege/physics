#include "model_files.h"
#include "editor_model.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace physics_test;

namespace
{
    editor_document sample()
    {
        editor_document doc;
        for (int i = 0; i < 4; ++i)
        {
            editor_mass m;
            m.m = 1.0 + i;
            m.r = 10.0;
            m.position = vector2(100.0 * i, 0.0);
            doc.masses.push_back(m);
        }
        doc.springs = { { 0, 1, 10, 100, 0.5, 1.5 }, { 1, 2, 11, 100, 0.5, 1.5 }, { 2, 3, 12, 100, 0.5, 1.5 } };
        doc.dampers = { { 1, 2, 0.7, 100 } };
        return doc;
    }

    editor_model make_model()
    {
        editor_model model;
        model.reset(sample());
        return model;
    }

    std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
}

TEST_CLASS(editor_model_tests)
{
public:
    TEST_METHOD(every_bundled_model_survives_the_editor_unchanged)
    {
        for (int n = 1; n <= 9; ++n)
        {
            const auto original = load_model(n);
            const auto doc = document_from_simulation(*original.sim, original.masses);

            loaded_simulation rebuilt;
            rebuilt.sim = build_simulation(doc, rebuilt.masses, rebuilt.springs, rebuilt.dampers);
            expect_same_simulation(original, rebuilt);
        }
    }

    TEST_METHOD(delete_removes_dangling_connections_and_reindexes)
    {
        auto model = make_model();
        model.select_mass(1, select_mode::replace);
        model.delete_selection();

        const auto& doc = model.document();
        Assert::AreEqual(size_t(3), doc.masses.size());
        Assert::AreEqual(size_t(1), doc.springs.size());
        Assert::AreEqual(size_t(1), doc.springs[0].a);
        Assert::AreEqual(size_t(2), doc.springs[0].b);
        Assert::AreEqual(12.0, doc.springs[0].k);
        Assert::AreEqual(size_t(0), doc.dampers.size());
        Assert::IsTrue(model.selection().empty());
    }

    TEST_METHOD(delete_of_a_selected_connection_keeps_the_masses)
    {
        auto model = make_model();
        const auto hit = model.pick_connection(vector2(150, 0), 5.0);
        Assert::IsTrue(hit.has_value());
        model.select_connection(*hit, select_mode::replace);
        model.delete_selection();

        Assert::AreEqual(size_t(4), model.document().masses.size());
        Assert::AreEqual(size_t(2), model.document().springs.size());
        Assert::AreEqual(size_t(0), model.document().dampers.size());
    }

    TEST_METHOD(copy_keeps_only_connections_between_selected_masses)
    {
        auto model = make_model();
        model.select_masses({ 1, 2 }, select_mode::replace);
        const auto text = model.copy_selection();
        Assert::IsFalse(text.empty());

        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        std::string error;
        const auto sim = load_simulation_xml_text(text, ms, ss, ds, error);
        Assert::IsNotNull(sim.get(), wide(error).c_str());
        Assert::AreEqual(size_t(2), ms.size());
        Assert::AreEqual(size_t(1), sim->model().springs().size());
        Assert::AreEqual(size_t(1), sim->model().dampers().size());
        Assert::AreEqual(size_t(0), sim->model().springs()[0].id_mass1());
        Assert::AreEqual(size_t(1), sim->model().springs()[0].id_mass2());
        Assert::AreEqual(11.0, sim->model().springs()[0].k());
    }

    TEST_METHOD(copy_with_nothing_selected_is_empty)
    {
        auto model = make_model();
        Assert::IsTrue(model.copy_selection().empty());
    }

    TEST_METHOD(paste_centres_on_the_anchor_shifts_indices_and_selects_the_copy)
    {
        auto model = make_model();
        model.select_masses({ 1, 2 }, select_mode::replace);
        const auto text = model.copy_selection();

        std::string error;
        Assert::IsTrue(model.paste_at(text, vector2(1000, 500), error), wide(error).c_str());

        const auto& doc = model.document();
        Assert::AreEqual(size_t(6), doc.masses.size());
        Assert::AreEqual(1000.0, (doc.masses[4].position.x() + doc.masses[5].position.x()) / 2, 1e-9);
        Assert::AreEqual(500.0, doc.masses[4].position.y(), 1e-9);
        Assert::AreEqual(size_t(4), doc.springs.size());
        Assert::AreEqual(size_t(4), doc.springs[3].a);
        Assert::AreEqual(size_t(5), doc.springs[3].b);
        Assert::AreEqual(size_t(2), model.selection().masses.size());
        Assert::IsTrue(model.selection().masses.count(4) && model.selection().masses.count(5));
        Assert::IsTrue(model.dirty());
    }

    TEST_METHOD(paste_preserves_mass_properties_and_velocity)
    {
        auto doc = sample();
        doc.masses[2].fixed = true;
        doc.masses[2].velocity = vector2(3, -4);
        doc.masses[2].e = 0.25;
        editor_model model;
        model.reset(doc);

        model.select_mass(2, select_mode::replace);
        const auto text = model.copy_selection();
        std::string error;
        Assert::IsTrue(model.paste_offset(text, vector2(5, 5), error));

        const auto& pasted = model.document().masses.back();
        Assert::AreEqual(3.0, pasted.m);
        Assert::AreEqual(0.25, pasted.e);
        Assert::IsTrue(pasted.fixed);
        Assert::AreEqual(3.0, pasted.velocity.x());
        Assert::AreEqual(-4.0, pasted.velocity.y());
        Assert::AreEqual(205.0, pasted.position.x());
        Assert::AreEqual(5.0, pasted.position.y());
    }

    TEST_METHOD(cut_then_paste_restores_the_masses_in_place)
    {
        auto model = make_model();
        model.select_masses({ 1, 2 }, select_mode::replace);
        const auto text = model.cut_selection();
        Assert::AreEqual(size_t(2), model.document().masses.size());
        Assert::AreEqual(size_t(0), model.document().dampers.size());

        std::string error;
        Assert::IsTrue(model.paste_offset(text, vector2(0, 0), error));
        Assert::AreEqual(size_t(4), model.document().masses.size());
        Assert::AreEqual(100.0, model.document().masses[2].position.x());
        Assert::AreEqual(200.0, model.document().masses[3].position.x());
        Assert::AreEqual(size_t(1), model.document().dampers.size());
    }

    TEST_METHOD(duplicate_offsets_the_copy_and_selects_it)
    {
        auto model = make_model();
        model.select_mass(0, select_mode::replace);
        Assert::IsTrue(model.duplicate_selection());
        Assert::AreEqual(size_t(5), model.document().masses.size());
        Assert::AreEqual(25.0, model.document().masses[4].position.x());
        Assert::IsTrue(model.selection().masses.count(4) == 1 && model.selection().masses.size() == 1);
    }

    TEST_METHOD(paste_rejects_text_that_is_not_a_simulation)
    {
        auto model = make_model();
        std::string error;
        Assert::IsFalse(model.paste_at("hello world", vector2(), error));
        Assert::IsFalse(error.empty());
        Assert::AreEqual(size_t(4), model.document().masses.size());
        Assert::IsFalse(model.can_undo());
    }

    TEST_METHOD(undo_and_redo_restore_document_and_selection)
    {
        auto model = make_model();
        model.add_mass(vector2(7, 7));
        Assert::AreEqual(size_t(5), model.document().masses.size());

        model.undo();
        Assert::AreEqual(size_t(4), model.document().masses.size());
        Assert::IsTrue(model.can_redo());

        model.redo();
        Assert::AreEqual(size_t(5), model.document().masses.size());
        Assert::AreEqual(7.0, model.document().masses[4].position.x());

        model.select_mass(1, select_mode::replace);
        model.delete_selection();
        model.undo();
        Assert::AreEqual(size_t(5), model.document().masses.size());
        Assert::AreEqual(size_t(3), model.document().springs.size());
        Assert::AreEqual(size_t(1), model.document().dampers.size());
        Assert::IsTrue(model.selection().masses.count(1) == 1);
    }

    TEST_METHOD(a_new_edit_clears_redo)
    {
        auto model = make_model();
        model.add_mass(vector2());
        model.undo();
        model.add_mass(vector2(1, 1));
        Assert::IsFalse(model.can_redo());
    }

    TEST_METHOD(a_drag_is_a_single_undo_step)
    {
        auto model = make_model();
        model.select_masses({ 0, 1 }, select_mode::replace);
        model.begin_gesture();
        for (int i = 0; i < 10; ++i)
            model.move_selection(vector2(1, 2));

        Assert::AreEqual(10.0, model.document().masses[0].position.x());
        Assert::AreEqual(110.0, model.document().masses[1].position.x());

        model.undo();
        Assert::AreEqual(0.0, model.document().masses[0].position.x());
        Assert::AreEqual(0.0, model.document().masses[0].position.y());
        Assert::AreEqual(100.0, model.document().masses[1].position.x());
    }

    TEST_METHOD(editing_the_selection_changes_every_selected_mass)
    {
        auto model = make_model();
        model.select_all();
        mass_edit edit;
        edit.m = 5.0;
        edit.vx = 2.0;
        edit.fixed = true;
        std::string error;
        Assert::IsTrue(model.edit_masses(edit, error));

        for (const auto& m : model.document().masses)
        {
            Assert::AreEqual(5.0, m.m);
            Assert::AreEqual(2.0, m.velocity.x());
            Assert::IsTrue(m.fixed);
        }
        Assert::AreEqual(100.0, model.document().masses[1].position.x());
    }

    TEST_METHOD(invalid_edits_are_rejected_without_changing_anything)
    {
        auto model = make_model();
        model.select_all();

        mass_edit edit;
        edit.m = -1.0;
        std::string error;
        Assert::IsFalse(model.edit_masses(edit, error));
        Assert::IsFalse(error.empty());
        Assert::AreEqual(1.0, model.document().masses[0].m);
        Assert::IsFalse(model.can_undo());

        editor_world world = model.document().world;
        world.xmax = world.xmin;
        Assert::IsFalse(model.edit_world(world, error));
        world = model.document().world;
        world.dt = 0;
        Assert::IsFalse(model.edit_world(world, error));
        world = model.document().world;
        world.iterations = 0;
        Assert::IsFalse(model.edit_world(world, error));
        world = model.document().world;
        world.speed = 0.0;
        Assert::IsFalse(model.edit_world(world, error));
        world.speed = -3.0;
        Assert::IsFalse(model.edit_world(world, error));
        world = model.document().world;
        world.softening = -1.0;
        Assert::IsFalse(model.edit_world(world, error));
        Assert::IsFalse(model.can_undo());

        world = model.document().world;
        world.speed = 250.0;
        Assert::IsTrue(model.edit_world(world, error));
        Assert::AreEqual(250.0, model.document().world.speed);
        {
            std::vector<mass_state> ms;
            std::vector<spring_state> ss;
            std::vector<damper_state> ds;
            Assert::AreEqual(250.0, build_simulation(model.document(), ms, ss, ds)->speed());
        }
        model.undo();
        Assert::AreEqual(1.0, model.document().world.speed);

        world = model.document().world;
        world.dt = 0.5;
        Assert::IsTrue(model.edit_world(world, error));
        Assert::AreEqual(0.5, model.document().world.dt);
    }

    TEST_METHOD(connect_starts_at_rest_and_does_not_duplicate)
    {
        auto model = make_model();
        Assert::IsTrue(model.connect(0, 3, connection_kind::both));
        const auto& doc = model.document();
        Assert::AreEqual(size_t(4), doc.springs.size());
        Assert::AreEqual(300.0, doc.springs.back().l0);
        Assert::AreEqual(size_t(2), doc.dampers.size());

        Assert::IsFalse(model.connect(3, 0, connection_kind::both));
        Assert::IsFalse(model.connect(1, 1, connection_kind::spring));
        Assert::IsFalse(model.connect(0, 9, connection_kind::spring));
        Assert::AreEqual(size_t(4), model.document().springs.size());
    }

    TEST_METHOD(editing_a_connection_changes_its_spring_and_damper)
    {
        auto model = make_model();
        const auto hit = model.pick_connection(vector2(150, 2), 5.0);
        Assert::IsTrue(hit.has_value());
        model.select_connection(*hit, select_mode::replace);

        spring_edit spring;
        spring.k = 99.0;
        damper_edit damper;
        damper.k = 3.0;
        std::string error;
        Assert::IsTrue(model.edit_connections(spring, damper, error), wide(error).c_str());
        Assert::AreEqual(99.0, model.document().springs[1].k);
        Assert::AreEqual(10.0, model.document().springs[0].k);
        Assert::AreEqual(3.0, model.document().dampers[0].k);

        spring_edit bad;
        bad.max_factor = 0.1;
        Assert::IsFalse(model.edit_connections(bad, {}, error));
        Assert::AreEqual(1.5, model.document().springs[1].max_factor);
    }

    TEST_METHOD(picking_prefers_the_topmost_mass_and_honours_the_margin)
    {
        editor_document doc;
        for (int i = 0; i < 2; ++i)
        {
            editor_mass m;
            m.r = 10;
            m.position = vector2(i * 5.0, 0);
            doc.masses.push_back(m);
        }
        editor_model model;
        model.reset(doc);

        Assert::AreEqual(size_t(1), *model.pick_mass(vector2(3, 0), 0));
        Assert::IsFalse(model.pick_mass(vector2(0, 30), 0).has_value());
        Assert::IsTrue(model.pick_mass(vector2(0, 12), 3).has_value());
    }

    TEST_METHOD(rectangle_selection_and_toggle)
    {
        auto model = make_model();
        const auto inside = model.masses_in_rect(vector2(250, 10), vector2(-10, -10));
        Assert::AreEqual(size_t(3), inside.size());

        model.select_masses(inside, select_mode::replace);
        model.select_mass(1, select_mode::toggle);
        Assert::AreEqual(size_t(2), model.selection().masses.size());
        model.select_mass(3, select_mode::add);
        Assert::AreEqual(size_t(3), model.selection().masses.size());
    }

    TEST_METHOD(added_masses_use_the_defaults)
    {
        auto model = make_model();
        model.defaults().mass.m = 42.0;
        model.defaults().mass.r = 7.0;
        const auto index = model.add_mass(vector2(1, 2));
        Assert::AreEqual(42.0, model.document().masses[index].m);
        Assert::AreEqual(7.0, model.document().masses[index].r);
        Assert::IsTrue(model.selection().masses.count(index) == 1);
    }

    TEST_METHOD(wall_flags_are_edited_undone_and_reach_the_simulation)
    {
        auto model = make_model();
        Assert::IsTrue(model.document().world.bounce_bottom);
        Assert::IsFalse(model.document().world.bounce_left);

        auto world = model.document().world;
        world.bounce_left = true;
        world.bounce_right = true;
        world.bounce_top = true;
        world.bounce_bottom = false;
        std::string error;
        Assert::IsTrue(model.edit_world(world, error));

        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        auto sim = build_simulation(model.document(), ms, ss, ds);
        Assert::IsTrue(sim->walls().left && sim->walls().right && sim->walls().top);
        Assert::IsFalse(sim->walls().bottom);

        model.undo();
        sim = build_simulation(model.document(), ms, ss, ds);
        Assert::IsFalse(sim->walls().left);
        Assert::IsTrue(sim->walls().bottom);
    }

    TEST_METHOD(colours_are_set_reset_and_kept_by_copy_and_paste)
    {
        auto model = make_model();
        model.select_masses({ 1, 2 }, select_mode::replace);
        mass_edit paint;
        paint.colour = 0x336699;
        std::string error;
        Assert::IsTrue(model.edit_masses(paint, error));
        Assert::AreEqual(uint32_t(0x336699), *model.document().masses[1].colour);
        Assert::AreEqual(uint32_t(0x336699), *model.document().masses[2].colour);
        Assert::IsFalse(model.document().masses[0].colour.has_value());

        const auto hit = model.pick_connection(vector2(150, 0), 5.0);
        model.select_connection(*hit, select_mode::replace);
        spring_edit line;
        line.colour = 0xFF00FF;
        Assert::IsTrue(model.edit_connections(line, {}, error));
        Assert::AreEqual(uint32_t(0xFF00FF), *model.document().springs[1].colour);

        // Colours survive the clipboard text.
        model.select_masses({ 1, 2 }, select_mode::replace);
        const auto text = model.copy_selection();
        Assert::IsTrue(model.paste_offset(text, vector2(0, 50), error));
        Assert::AreEqual(uint32_t(0x336699), *model.document().masses[4].colour);
        Assert::AreEqual(uint32_t(0xFF00FF), *model.document().springs.back().colour);

        // Reset returns to the default colour, and undo brings the colour back.
        model.select_masses({ 1 }, select_mode::replace);
        mass_edit reset;
        reset.reset_colour = true;
        Assert::IsTrue(model.edit_masses(reset, error));
        Assert::IsFalse(model.document().masses[1].colour.has_value());
        model.undo();
        Assert::IsTrue(model.document().masses[1].colour.has_value());
    }

    TEST_METHOD(labels_are_set_reset_and_kept_by_copy_and_paste_and_reach_the_run)
    {
        auto model = make_model();
        model.select_masses({ 1, 2 }, select_mode::replace);
        mass_edit name;
        name.label = std::string("Probe");
        std::string error;
        Assert::IsTrue(model.edit_masses(name, error));
        Assert::IsTrue(*model.document().masses[1].label == "Probe");
        Assert::IsFalse(model.document().masses[0].label.has_value());

        const auto text = model.copy_selection();
        Assert::IsTrue(model.paste_offset(text, vector2(0, 50), error));
        Assert::IsTrue(*model.document().masses[4].label == "Probe");

        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        const auto sim = build_simulation(model.document(), ms, ss, ds);
        Assert::IsTrue(*sim->model().masses()[2].label() == "Probe");
        Assert::IsFalse(sim->model().masses()[0].label().has_value());

        model.select_masses({ 1 }, select_mode::replace);
        mass_edit reset;
        reset.reset_label = true;
        Assert::IsTrue(model.edit_masses(reset, error));
        Assert::IsFalse(model.document().masses[1].label.has_value());
        model.undo();
        Assert::IsTrue(model.document().masses[1].label.has_value());
    }

    TEST_METHOD(colours_reach_the_simulation_that_runs)
    {
        auto model = make_model();
        model.select_mass(0, select_mode::replace);
        mass_edit paint;
        paint.colour = 0x102030;
        std::string error;
        Assert::IsTrue(model.edit_masses(paint, error));

        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        const auto sim = build_simulation(model.document(), ms, ss, ds);
        Assert::AreEqual(uint32_t(0x102030), *sim->model().masses()[0].colour());
        Assert::IsFalse(sim->model().masses()[1].colour().has_value());
    }

    TEST_METHOD(an_edited_document_runs_as_a_simulation)
    {
        auto model = make_model();
        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        const auto sim = build_simulation(model.document(), ms, ss, ds);
        Assert::AreEqual(size_t(4), sim->model().masses().size());
        Assert::AreEqual(size_t(4), ms.size());
        Assert::AreEqual(0.1, sim->dt());
    }
};
