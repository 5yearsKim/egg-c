#!/usr/bin/env python3
"""Focused checks for reproducibility and failure reduction."""

from types import SimpleNamespace
from unittest import TestCase, main, mock

import check


class HarnessTests(TestCase):
    def test_seeded_cases_are_reproducible(self):
        self.assertEqual(check.core_case(123), check.core_case(123))
        self.assertEqual(check.arithmetic_case(123), check.arithmetic_case(123))
        self.assertNotEqual(check.core_case(123), check.core_case(124))

    def test_reducer_removes_irrelevant_operations(self):
        source = "\n".join((
            "version\t1",
            "add\ta\ta",
            "add\tb\tb",
            "add\tunused\t(f a)",
            "rebuild",
            "eq\ta\tb",
            "add\textra\t(g b)",
            "rebuild",
        )) + "\n"

        def fake_execute(binary, candidate):
            handles = {line.split("\t")[1] for line in candidate.splitlines()
                       if line.startswith("add\t")}
            valid = "a" in handles and "b" in handles and "eq\ta\tb" in candidate
            return SimpleNamespace(returncode=0 if valid else 2,
                                   stdout=f"eq\t{1 if binary == 'cpp' else 0}\n" if valid else "",
                                   stderr="")

        with mock.patch.object(check, "execute", side_effect=fake_execute):
            reduced = check.reduce_case(source, "cpp", "rust", "eq")
            self.assertTrue(check.valid_reduction(reduced, "cpp", "rust", "eq"))
        self.assertLess(len(reduced.splitlines()), len(source.splitlines()))
        self.assertIn("eq\ta\tb", reduced)


if __name__ == "__main__":
    main()
