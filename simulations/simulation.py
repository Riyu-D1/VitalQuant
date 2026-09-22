import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import find_peaks


x = np.linspace(1600, 1700, 2000)


def gaussian(axis: np.ndarray, center: float, width: float, amplitude: float) -> np.ndarray:
    return amplitude * np.exp(-((axis - center) ** 2) / (2 * width**2))


def classical_detection_signal(axis: np.ndarray, aggregation_fraction: float, width: float = 8.0, amplitude: float = 1000.0) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    normal_peak = gaussian(axis, 1650.0, width, amplitude)
    disease_peak = gaussian(axis, 1650.0 + 20.0 * aggregation_fraction, width, amplitude)
    expected_counts = normal_peak + disease_peak
    measured_counts = np.random.poisson(np.maximum(expected_counts, 0.0))
    return normal_peak, disease_peak, measured_counts


def quantum_detection_signal(axis: np.ndarray, aggregation_fraction: float, squeezing_parameter: float, width: float = 8.0, amplitude: float = 1000.0) -> tuple[np.ndarray, np.ndarray]:
    disease_peak = gaussian(axis, 1650.0 + 20.0 * aggregation_fraction, width, amplitude)
    quantum_noise_sigma = np.sqrt(np.maximum(disease_peak, 1.0)) * np.exp(-squeezing_parameter)
    measured_signal = disease_peak + np.random.normal(0.0, quantum_noise_sigma)
    return disease_peak, measured_signal


def estimate_peak_center(axis: np.ndarray, spectrum: np.ndarray) -> float:
    peaks, _ = find_peaks(spectrum)
    if peaks.size == 0:
        return float(axis[np.argmax(spectrum)])
    dominant_peak = peaks[np.argmax(spectrum[peaks])]
    return float(axis[dominant_peak])


def snr(signal: np.ndarray, reference: np.ndarray, noise_std: np.ndarray) -> float:
    delta = signal - reference
    return float(np.max(np.abs(delta)) / (np.mean(noise_std) + 1e-9))


def run_single_case(aggregation_fraction: float = 0.4, squeezing_parameter: float = 0.8) -> dict[str, np.ndarray | float]:
    normal_peak, disease_peak, classical_measured = classical_detection_signal(x, aggregation_fraction)
    _, quantum_measured = quantum_detection_signal(x, aggregation_fraction, squeezing_parameter)

    classical_noise_std = np.sqrt(np.maximum(normal_peak + disease_peak, 1.0))
    quantum_noise_std = np.sqrt(np.maximum(disease_peak, 1.0)) * np.exp(-squeezing_parameter)

    return {
        "normal_peak": normal_peak,
        "disease_peak": disease_peak,
        "classical_measured": classical_measured,
        "quantum_measured": quantum_measured,
        "classical_snr": snr(disease_peak, normal_peak, classical_noise_std),
        "quantum_snr": snr(disease_peak, normal_peak, quantum_noise_std),
        "classical_peak_center": estimate_peak_center(x, classical_measured),
        "quantum_peak_center": estimate_peak_center(x, quantum_measured),
    }


def detectable_threshold(snr_threshold: float = 3.0, squeezing_parameter: float = 0.8) -> tuple[float, float]:
    aggregation_grid = np.linspace(0.0, 1.0, 101)
    classical_threshold = np.nan
    quantum_threshold = np.nan

    for aggregation_fraction in aggregation_grid:
        normal_peak, disease_peak, _ = classical_detection_signal(x, aggregation_fraction)
        _, _ = quantum_detection_signal(x, aggregation_fraction, squeezing_parameter)

        classical_noise_std = np.sqrt(np.maximum(normal_peak + disease_peak, 1.0))
        quantum_noise_std = np.sqrt(np.maximum(disease_peak, 1.0)) * np.exp(-squeezing_parameter)

        classical_snr_val = snr(disease_peak, normal_peak, classical_noise_std)
        quantum_snr_val = snr(disease_peak, normal_peak, quantum_noise_std)

        if np.isnan(classical_threshold) and classical_snr_val >= snr_threshold:
            classical_threshold = float(aggregation_fraction)
        if np.isnan(quantum_threshold) and quantum_snr_val >= snr_threshold:
            quantum_threshold = float(aggregation_fraction)

    return classical_threshold, quantum_threshold


def sweep_snr_vs_squeezing(aggregation_fraction: float = 0.4) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    squeezing_values = np.linspace(0.0, 1.5, 80)
    classical_snr_values = []
    quantum_snr_values = []

    normal_peak, disease_peak, _ = classical_detection_signal(x, aggregation_fraction)
    classical_noise_std = np.sqrt(np.maximum(normal_peak + disease_peak, 1.0))
    classical_snr_constant = snr(disease_peak, normal_peak, classical_noise_std)

    for squeezing_parameter in squeezing_values:
        quantum_noise_std = np.sqrt(np.maximum(disease_peak, 1.0)) * np.exp(-squeezing_parameter)
        quantum_snr_values.append(snr(disease_peak, normal_peak, quantum_noise_std))
        classical_snr_values.append(classical_snr_constant)

    return squeezing_values, np.array(classical_snr_values), np.array(quantum_snr_values)


def aggregation_surface(squeezing_parameter: float = 0.8) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    aggregation_grid = np.linspace(0.0, 1.0, 50)
    surface = np.zeros((aggregation_grid.size, x.size))

    for i, aggregation_fraction in enumerate(aggregation_grid):
        disease_peak = gaussian(x, 1650.0 + 20.0 * aggregation_fraction, 8.0, 1000.0)
        quantum_noise_std = np.sqrt(np.maximum(disease_peak, 1.0)) * np.exp(-squeezing_parameter)
        surface[i, :] = disease_peak + np.random.normal(0.0, quantum_noise_std)

    return aggregation_grid, x, surface


def main() -> None:
    np.random.seed(7)

    aggregation_fraction = 0.4
    squeezing_parameter = 0.8

    case = run_single_case(aggregation_fraction=aggregation_fraction, squeezing_parameter=squeezing_parameter)
    classical_threshold, quantum_threshold = detectable_threshold(snr_threshold=3.0, squeezing_parameter=squeezing_parameter)
    squeeze_axis, classical_snr_curve, quantum_snr_curve = sweep_snr_vs_squeezing(aggregation_fraction=aggregation_fraction)
    agg_grid, x_axis, z_surface = aggregation_surface(squeezing_parameter=squeezing_parameter)

    fig, axes = plt.subplots(2, 2, figsize=(13, 10))

    axes[0, 0].plot(x, case["classical_measured"], label="Classical detection", lw=1.6)
    axes[0, 0].plot(x, case["quantum_measured"], label="Quantum-enhanced detection", lw=1.6)
    axes[0, 0].set_xlabel("Raman Shift (cm⁻¹)")
    axes[0, 0].set_ylabel("Intensity")
    axes[0, 0].set_title("Simulated Detection of α-Synuclein Misfolding Signatures")
    axes[0, 0].legend()

    aggregation_sweep = np.linspace(0.0, 1.0, 50)
    measured_centers = [estimate_peak_center(x, classical_detection_signal(x, value)[2]) for value in aggregation_sweep]
    model_centers = 1650.0 + 20.0 * aggregation_sweep
    axes[0, 1].plot(aggregation_sweep, model_centers, label="Model center", lw=2)
    axes[0, 1].plot(aggregation_sweep, measured_centers, label="Measured center (classical)", lw=1.3)
    axes[0, 1].set_xlabel("Aggregation Fraction A")
    axes[0, 1].set_ylabel("Peak Position (cm⁻¹)")
    axes[0, 1].set_title("Aggregation-Driven Spectral Shift")
    axes[0, 1].legend()

    axes[1, 0].plot(squeeze_axis, classical_snr_curve, label="Classical SNR", lw=2)
    axes[1, 0].plot(squeeze_axis, quantum_snr_curve, label="Quantum SNR", lw=2)
    axes[1, 0].set_xlabel("Squeezing Parameter r")
    axes[1, 0].set_ylabel("SNR")
    axes[1, 0].set_title("SNR vs Squeezing")
    axes[1, 0].legend()

    labels = ["Classical", "Quantum"]
    thresholds = [classical_threshold, quantum_threshold]
    axes[1, 1].bar(labels, thresholds)
    axes[1, 1].set_ylim(0, 1)
    axes[1, 1].set_ylabel("Minimum Detectable Aggregation Fraction")
    axes[1, 1].set_title("Detection Threshold (SNR ≥ 3)")

    fig.tight_layout()

    fig3d = plt.figure(figsize=(10, 7))
    ax3d = fig3d.add_subplot(111, projection="3d")
    X, Y = np.meshgrid(x_axis, agg_grid)
    ax3d.plot_surface(X, Y, z_surface, cmap="viridis", linewidth=0, antialiased=True)
    ax3d.set_xlabel("Raman Shift (cm⁻¹)")
    ax3d.set_ylabel("Aggregation Fraction A")
    ax3d.set_zlabel("Intensity")
    ax3d.set_title("Quantum-Enhanced Raman Spectral Surface")

    print(f"Classical SNR: {case['classical_snr']:.2f}")
    print(f"Quantum SNR: {case['quantum_snr']:.2f}")
    print(f"Classical peak center estimate: {case['classical_peak_center']:.2f} cm⁻¹")
    print(f"Quantum peak center estimate: {case['quantum_peak_center']:.2f} cm⁻¹")
    print(f"Minimum detectable A (classical, SNR≥3): {classical_threshold:.2f}")
    print(f"Minimum detectable A (quantum, SNR≥3): {quantum_threshold:.2f}")
    print("Model intent: structural biomarker simulation under noise, not clinical diagnosis.")

    plt.show()


if __name__ == "__main__":
    main()
