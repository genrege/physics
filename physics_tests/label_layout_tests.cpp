#include "CppUnitTest.h"
#include "label_layout.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    label_size fixed_size(size_t) { return { 40.0f, 12.0f }; }

    label_body body(size_t id, float x, float y, float radius, double priority, bool labelled = true)
    {
        label_body b;
        b.id = id;
        b.x = x;
        b.y = y;
        b.radius = radius;
        b.priority = priority;
        b.labelled = labelled;
        return b;
    }

    bool contains(const std::vector<label_placement>& placed, size_t id)
    {
        for (const auto& p : placed)
        {
            if (p.id == id)
                return true;
        }
        return false;
    }
}

TEST_CLASS(label_layout_tests)
{
public:
    TEST_METHOD(masses_are_shown_in_kilograms_with_a_short_exponent)
    {
        Assert::AreEqual(std::string("1.99e30 kg"), format_mass(1.989e30));
        Assert::AreEqual(std::string("5.97e24 kg"), format_mass(5.9722e24));
        Assert::AreEqual(std::string("100 kg"), format_mass(100.0));
        Assert::AreEqual(std::string("0.5 kg"), format_mass(0.5));
        Assert::AreEqual(std::string("5e-5 kg"), format_mass(5e-5));
        Assert::AreEqual(std::string("1e100 kg"), format_mass(1e100));
    }

    TEST_METHOD(a_label_defaults_to_the_mass_and_an_explicit_one_wins)
    {
        Assert::AreEqual(std::string("2.5 kg"), label_text(mass(2.5, 1.0)));
        Assert::AreEqual(std::string("Earth"), label_text(mass(5.97e24, 1.0, 1.0, std::nullopt, std::string("Earth"))));
        Assert::AreEqual(std::string(), label_text(mass(1.0, 1.0, 1.0, std::nullopt, std::string())));
    }

    TEST_METHOD(a_lone_body_gets_its_label_to_the_right)
    {
        const auto placed = place_labels({ body(7, 100, 100, 5, 1) }, 400, 300, fixed_size);
        Assert::AreEqual(size_t(1), placed.size());
        Assert::AreEqual(size_t(7), placed[0].id);
        Assert::IsTrue(placed[0].left > 100.0f + 5.0f);
        Assert::AreEqual(100.0f - 6.0f, placed[0].top);
    }

    TEST_METHOD(the_label_moves_to_the_left_near_the_right_edge)
    {
        const auto placed = place_labels({ body(1, 380, 100, 5, 1) }, 400, 300, fixed_size);
        Assert::AreEqual(size_t(1), placed.size());
        Assert::IsTrue(placed[0].left + 40.0f < 380.0f);
    }

    TEST_METHOD(the_heavier_body_wins_a_contested_spot)
    {
        // Two bodies a few pixels apart near the left edge: their labels would overlap and the left side is off screen.
        const auto placed = place_labels({ body(1, 5, 100, 3, 10), body(2, 9, 103, 3, 500) }, 400, 300, fixed_size);
        Assert::IsTrue(contains(placed, 2));
        Assert::IsFalse(contains(placed, 1));
    }

    TEST_METHOD(a_label_does_not_cover_another_labelled_body)
    {
        // The only free spot to the right of body 1 is occupied by body 2, and the left side is off screen.
        const auto placed = place_labels({ body(1, 5, 100, 3, 10), body(2, 30, 100, 3, 1) }, 400, 300, fixed_size);
        Assert::IsFalse(contains(placed, 1));
    }

    TEST_METHOD(bodies_without_labels_neither_get_one_nor_block_one)
    {
        // Body 1 sits where body 2's label would go.
        const auto placed = place_labels({ body(1, 120, 100, 3, 10, false), body(2, 100, 100, 3, 1) }, 400, 300, fixed_size);
        Assert::AreEqual(size_t(1), placed.size());
        Assert::AreEqual(size_t(2), placed[0].id);
    }

    TEST_METHOD(zooming_out_leaves_only_the_most_important_labels)
    {
        auto scene = [](float spacing) {
            std::vector<label_body> bodies;
            for (size_t i = 0; i < 20; ++i)
                bodies.push_back(body(i, 50.0f + float(i) * spacing, 100, 2, double(i)));
            return bodies;
        };
        const auto zoomed_in = place_labels(scene(60.0f), 2000, 300, fixed_size);
        const auto zoomed_out = place_labels(scene(6.0f), 2000, 300, fixed_size);
        Assert::AreEqual(size_t(20), zoomed_in.size());
        Assert::IsTrue(zoomed_out.size() < 6);
        Assert::IsTrue(contains(zoomed_out, 19));   // the heaviest survives
    }

    TEST_METHOD(placed_labels_never_overlap_and_stay_inside_the_view)
    {
        std::vector<label_body> bodies;
        for (size_t i = 0; i < 400; ++i)
            bodies.push_back(body(i, float((i * 37) % 500), float((i * 91) % 300), 2, double(i % 17)));

        const auto placed = place_labels(bodies, 500, 300, fixed_size);
        Assert::IsFalse(placed.empty());
        for (size_t a = 0; a < placed.size(); ++a)
        {
            Assert::IsTrue(placed[a].left >= 0 && placed[a].top >= 0 && placed[a].left + 40.0f <= 500 && placed[a].top + 12.0f <= 300);
            for (size_t b = a + 1; b < placed.size(); ++b)
            {
                const bool apart = placed[a].left >= placed[b].left + 40.0f || placed[b].left >= placed[a].left + 40.0f ||
                    placed[a].top >= placed[b].top + 12.0f || placed[b].top >= placed[a].top + 12.0f;
                Assert::IsTrue(apart);
            }
        }
    }

    TEST_METHOD(bodies_outside_the_view_are_ignored_and_the_count_is_capped)
    {
        Assert::IsTrue(place_labels({ body(1, -500, 100, 3, 1) }, 400, 300, fixed_size).empty());

        std::vector<label_body> bodies;
        for (size_t i = 0; i < 50; ++i)
            bodies.push_back(body(i, 20.0f + float(i) * 80.0f, 100, 2, 1));
        label_options options;
        options.max_labels = 5;
        Assert::AreEqual(size_t(5), place_labels(bodies, 5000, 300, fixed_size, options).size());
    }

    TEST_METHOD(empty_text_is_not_placed)
    {
        const auto placed = place_labels({ body(1, 100, 100, 3, 1) }, 400, 300, [](size_t) { return label_size{ 0.0f, 0.0f }; });
        Assert::IsTrue(placed.empty());
    }
};
