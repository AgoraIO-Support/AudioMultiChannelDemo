import Foundation

/// 极简 RIFF/WAVE 解析：仅支持 pcm_s16le。非法位深/采样率直接抛错（与 Linux WAV sender 同口径：
/// 多声道仅 48 kHz，24bit 转换与重采样不在本 demo 范围内）。
struct WavFile {
    let channels: Int
    let sampleRate: Int
    let bitsPerSample: Int
    /// 交错 s16le PCM 数据（data chunk 原文）
    let pcmData: Data

    var durationSeconds: Double {
        let bytesPerSecond = Double(sampleRate * channels * (bitsPerSample / 8))
        return bytesPerSecond > 0 ? Double(pcmData.count) / bytesPerSecond : 0
    }

    enum WavError: Error, LocalizedError {
        case notRiff
        case noFmtChunk
        case noDataChunk
        case unsupportedFormat(Int)
        case unsupportedBits(Int)
        case unsupportedSampleRate(Int)

        var errorDescription: String? {
            switch self {
            case .notRiff: return "Not a RIFF/WAVE file"
            case .noFmtChunk: return "fmt chunk not found"
            case .noDataChunk: return "data chunk not found"
            case .unsupportedFormat(let f): return "Unsupported audio format \(f) (only PCM=1)"
            case .unsupportedBits(let b): return "Unsupported bit depth \(b) (only 16-bit)"
            case .unsupportedSampleRate(let r): return "Unsupported sample rate \(r) (multi-channel requires 48000)"
            }
        }
    }

    static func load(url: URL) throws -> WavFile {
        let data = try Data(contentsOf: url)
        guard data.count > 44,
              String(decoding: data[0..<4], as: UTF8.self) == "RIFF",
              String(decoding: data[8..<12], as: UTF8.self) == "WAVE" else {
            throw WavError.notRiff
        }
        var offset = 12
        var channels = 0, sampleRate = 0, bitsPerSample = 0, audioFormat = 0
        var pcm = Data()
        while offset + 8 <= data.count {
            let chunkId = String(decoding: data[offset..<offset + 4], as: UTF8.self)
            let chunkSize = Int(readUInt32LE(data, offset + 4))
            let bodyStart = offset + 8
            guard bodyStart <= data.count else { break }
            let bodyEnd = min(bodyStart + chunkSize, data.count)
            if chunkId == "fmt " {
                guard chunkSize >= 16, bodyEnd - bodyStart >= 16 else { throw WavError.noFmtChunk }
                audioFormat = Int(readUInt16LE(data, bodyStart))
                channels = Int(readUInt16LE(data, bodyStart + 2))
                sampleRate = Int(readUInt32LE(data, bodyStart + 4))
                bitsPerSample = Int(readUInt16LE(data, bodyStart + 14))
                // WAVE_FORMAT_EXTENSIBLE(0xFFFE)：真实格式在 SubFormat GUID 首 2 字节
                // （1 = PCM），GUID 尾部须为标准 {00000000-0000-0010-8000-00AA00389B71}
                if audioFormat == 0xFFFE {
                    guard chunkSize >= 40, bodyEnd - bodyStart >= 40 else { throw WavError.noFmtChunk }
                    let subFormat = Int(readUInt16LE(data, bodyStart + 24))
                    let guidTail = data[(bodyStart + 26)..<(bodyStart + 40)]
                    let standardTail = Data([0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                             0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71])
                    guard guidTail == standardTail else { throw WavError.unsupportedFormat(audioFormat) }
                    audioFormat = subFormat
                }
            } else if chunkId == "data" {
                pcm = data.subdata(in: bodyStart..<bodyEnd)
            }
            offset = bodyStart + chunkSize + (chunkSize & 1)
        }
        guard audioFormat != 0 else { throw WavError.noFmtChunk }
        guard audioFormat == 1 else { throw WavError.unsupportedFormat(audioFormat) }
        guard bitsPerSample == 16 else { throw WavError.unsupportedBits(bitsPerSample) }
        guard sampleRate == 48000 else { throw WavError.unsupportedSampleRate(sampleRate) }
        guard !pcm.isEmpty else { throw WavError.noDataChunk }
        return WavFile(channels: channels, sampleRate: sampleRate,
                       bitsPerSample: bitsPerSample, pcmData: pcm)
    }

    private static func readUInt16LE(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | UInt16(data[offset + 1]) << 8
    }

    private static func readUInt32LE(_ data: Data, _ offset: Int) -> UInt32 {
        UInt32(data[offset]) | UInt32(data[offset + 1]) << 8
            | UInt32(data[offset + 2]) << 16 | UInt32(data[offset + 3]) << 24
    }
}
