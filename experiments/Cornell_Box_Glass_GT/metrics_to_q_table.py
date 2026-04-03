import pandas as pd


INPUT_CSV = "./Caustics/metrics2026-04-03_16-45-25.csv"
OUTPUT_HPP = "q_table_generated.hpp"
TABLE_NAME = "Q_TABLE_CAUSTICS_3"


def main() -> None:
    # Load CSV
    df = pd.read_csv(INPUT_CSV)

    # Validate required columns
    required_columns = ["State", "Q0", "Q1", "Q2"]
    missing = [col for col in required_columns if col not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

    # Keep only the relevant columns
    df_clean = df[["State", "Q0", "Q1", "Q2"]].copy()

    # Take the latest row for each observed state
    # groupby(...).last() preserves the last occurrence in file order
    df_final = (
        df_clean.groupby("State", as_index=False)
        .last()
        .sort_values("State")
    )

    # Build .hpp content
    lines = [
        "#pragma once",
        "#include <array>",
        "#include <unordered_map>",
        "",
        f"static const std::unordered_map<int, std::array<float, 3>> {TABLE_NAME} = {{"
    ]

    for _, row in df_final.iterrows():
        state = int(row["State"])
        q0 = float(row["Q0"])
        q1 = float(row["Q1"])
        q2 = float(row["Q2"])

        lines.append(
            f"    {{{state}, {{{q0:.9f}f, {q1:.9f}f, {q2:.9f}f}}}},"
        )

    lines.append("};")

    # Write output file
    with open(OUTPUT_HPP, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Generated {OUTPUT_HPP} with {len(df_final)} states.")


if __name__ == "__main__":
    main()

