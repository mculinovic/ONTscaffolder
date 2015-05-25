#include <seqan/sequence.h>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>
#include <utility>
#include <memory>
#include <unordered_map>

#include "./bwa.h"
#include "./utility.h"
#include "./scaffolder.h"
#include "./extension.h"
#include "./bases.h"
#include "poa/src/poa.hpp"

#define UNMAPPED 0x4
#define INNER_MARGIN 5  // margin for soft clipping port on read ends
#define OUTER_MARGIN 15
#define MIN_COVERAGE 10  // minimum coverage for position
#define EXT_MAX_LENGTH 1000

using std::vector;
using std::string;
using std::reverse;
using std::pair;
using std::shared_ptr;
using std::unordered_map;

using seqan::CharString;
using seqan::Dna5String;
using seqan::StringSet;
using seqan::BamAlignmentRecord;
using seqan::toCString;
using seqan::String;
using seqan::CStyle;
using seqan::length;

using bases::BasesCounter;


namespace scaffolder {


void find_possible_extensions(const vector<BamAlignmentRecord>& aln_records,
                              vector<shared_ptr<Extension>>* pleft_ext_reads,
                              vector<shared_ptr<Extension>>* pright_ext_reads,
                              const unordered_map<string, uint32_t>&
                              read_name_to_id,
                              uint64_t contig_len) {
    auto& left_ext_reads = *pleft_ext_reads;
    auto& right_ext_reads = *pright_ext_reads;

    // used to determine read length from cigar string
    auto contributes_to_seq_len = [](char c) -> int {
        switch (c) {
            case 'M': return 1;  // alignment match
            case 'I': return 1;  // insertion to reference
            case 'S': return 1;  // soft clipping
            case 'X': return 1;  // sequence mismatch
            case '=': return 1;  // sequence match
            default: return 0;
        }
    };

    // used to determine length of contig part to which
    // read is aligned
    auto contributes_to_contig_len = [](char c) -> int {
        switch (c) {
            case 'M': return 1;  // alignment match
            case 'D': return 1;  // deletion from reference
            case 'X': return 1;  // sequence mismatch
            case '=': return 1;  // sequence match
            default: return 0;
        }
    };

    for (auto const& record : aln_records) {
        // get read name as cpp string
        String<char, CStyle> tmp_name = record.qName;
        string read_name(tmp_name);

        // record is suitable for extending contig
        // if it is soft clipped and clipped part extends
        // left of contig start
        // example:
        // contig ->     ------------
        // read ->  ----------
        if ((record.flag & UNMAPPED) == 0 &&
            record.cigar[0].operation == 'S' &&
            record.beginPos < OUTER_MARGIN &&
            record.cigar[0].count > (uint32_t) record.beginPos) {
            // length of extension
            int len = record.cigar[0].count - record.beginPos;
            String<char, CStyle> tmp = record.seq;
            string seq(tmp);
            // std::cout << record.qName << " " << len << std::endl;

            uint32_t read_id = read_name_to_id.find(read_name)->second;

            if (record.beginPos < INNER_MARGIN) {
                int start = len <= EXT_MAX_LENGTH ? 0 : len - EXT_MAX_LENGTH;
                string extension = seq.substr(start, EXT_MAX_LENGTH);
                // reverse it because when searching for next base
                // in contig extension on left side we're moving
                // in direction right to left: <--------
                reverse(extension.begin(), extension.end());

                shared_ptr<Extension> ext(new Extension(
                    read_id, extension, false));
                left_ext_reads.emplace_back(ext);
            } else {
                shared_ptr<Extension> ext(new Extension(
                    read_id, string(), true));
                left_ext_reads.emplace_back(ext);
            }
        }

        int cigar_len = length(record.cigar);

        // if it is soft clipped and clipped part extends
        // right of contig start
        // example:
        // contig ->  ------------
        // read ->            ----------
        if ((record.flag & UNMAPPED) == 0 &&
            record.cigar[cigar_len - 1].operation == 'S') {
            // iterate over cigar string to get lengths of
            // read and contig parts used in alignment
            int used_read_size = 0;
            int used_contig_size = 0;
            for (auto const& e : record.cigar) {
                if (contributes_to_seq_len(e.operation)) {
                    used_read_size += e.count;
                }
                if (contributes_to_contig_len(e.operation)) {
                    used_contig_size += e.count;
                }
            }

            int right_clipping_len = record.cigar[cigar_len - 1].count;
            used_read_size -= right_clipping_len;
            int len = right_clipping_len -
                      (contig_len - (record.beginPos + used_contig_size));
            int margin = contig_len - (record.beginPos + used_contig_size);

            // if alignment ends more than 10 bases apart from contig
            // end skip read
            if (margin > OUTER_MARGIN) {
                continue;
            }

            // if read doesn't extend right of contig skip it
            if (len <= 0) {
                continue;
            }

            String<char, CStyle> tmp = record.seq;
            string seq(tmp);

            string extension = seq.substr(
                used_read_size + (right_clipping_len - len), EXT_MAX_LENGTH);

            uint32_t read_id = read_name_to_id.find(read_name)->second;
            bool drop = margin > INNER_MARGIN;
            shared_ptr<Extension> ext(new Extension(read_id,
                drop ? string() : extension, drop));
            right_ext_reads.emplace_back(ext);
        }
    }
}


string get_extension_mv_simple(
    const vector<shared_ptr<Extension>>& extensions) {
    // calculate extension by majority vote
    string extension("");

    for (uint32_t i = 0; true; ++i) {
        BasesCounter bases = bases::count_bases(extensions);

        if (bases.coverage >= MIN_COVERAGE) {
            char output_base = utility::idx_to_base(bases.max_idx);
            extension.push_back(output_base);

            std::cout << i << "\t" << output_base << "\t";
            for (int i = 0; i < NUM_BASES; ++i) {
                std::cout << bases.count[i] << "\t";
            }
            std::cout << std::endl;
        } else {
            // break when coverage below minimum
            break;
        }
    }

    return extension;
}


string get_extension_mv_realign(
    const vector<shared_ptr<Extension>>& extensions) {
    string contig_ext("");

    for (uint32_t i = 0; true; ++i) {
        BasesCounter bases = bases::count_bases(extensions);

        if (bases.coverage >= MIN_COVERAGE) {
            char output_base = utility::idx_to_base(bases.max_idx);
            contig_ext.push_back(output_base);

            // test output
            std::cout << i << "\t" << output_base << "\t";
            for (int i = 0; i < NUM_BASES; ++i) {
                std::cout << bases.count[i] << "\t";
            }
            std::cout << std::endl;

            // realignment
            auto is_read_eligible = [output_base](char c) -> bool {
                return c == output_base;
            };

            // majority vote for next base
            BasesCounter next_bases = bases::count_bases(extensions,
                                                         is_read_eligible,
                                                         1);

            char next_mv = utility::idx_to_base(next_bases.max_idx);

            if (next_bases.coverage < 0.6 * MIN_COVERAGE) {
                std::cout << "coverage: " << bases.coverage << std::endl;
                std::cout << "next_max_idx: " << next_bases.max_idx;
                std::cout << std::endl<< "next coverage: ";
                std::cout << next_bases.coverage << std::endl;
                break;
            }

            // cigar operation check
            for (size_t j = 0; j < extensions.size(); ++j) {
                auto& extension = extensions[j];
                auto& seq = extension->seq();

                // skip dropped reads
                if (extension->is_droped) {
                    continue;
                }

                // skip used reads
                if (extension->curr_pos() >= seq.length() - 1) {
                    extension->is_droped = true;
                    continue;
                }

                char current_base = seq[extension->curr_pos()];
                char next_base = seq[extension->curr_pos() + 1];

                if (current_base == output_base) {
                    // if operation is hit move forward
                    extension->do_operation(match);
                } else if (current_base == next_mv) {
                    // if operation is a deletion stay - do nothing
                    extension->do_operation(deletion_1);
                } else if (next_base == next_mv) {
                    // if operation is a mismatch move forward
                    extension->do_operation(mismatch);
                } else if (next_base == output_base) {
                    // if operation is an insertion skip one and
                    // move to the next one
                    extension->do_operation(insertion_1);
                } else {
                    // drop read
                    extension->is_droped = true;
                }
            }

        } else {
            // break when coverage below minimum
            std::cout << "coverage: " << bases.coverage << std::endl;
            break;
        }
    }

    return contig_ext;
}


Dna5String extend_contig(Dna5String* pcontig_seq,
                         const vector<BamAlignmentRecord>& aln_records,
                         const unordered_map<string, uint32_t>& read_name_to_id,
                         const StringSet<CharString>& read_ids,
                         const StringSet<Dna5String>& read_seqs) {
    vector<shared_ptr<Extension>> left_extensions;
    vector<shared_ptr<Extension>> right_extensions;
    auto& contig_seq = *pcontig_seq;

    find_possible_extensions(aln_records,
                         &left_extensions,
                         &right_extensions,
                         read_name_to_id,
                         length(contig_seq));

    while (true) {
        std::cout << "Left extension:" << std::endl;
        string left_extension = get_extension_mv_realign(left_extensions);
        reverse(left_extension.begin(), left_extension.end());

        std::cout << "Right extension:" << std::endl;
        string right_extension = get_extension_mv_realign(right_extensions);

        Dna5String tmp_contig_seq = left_extension;
        tmp_contig_seq += contig_seq;
        tmp_contig_seq += right_extension;
        contig_seq = tmp_contig_seq;
        // Dna5String extended_contig = left_extension;
        // extended_contig += contig_seq;
        // extended_contig += right_extension;

        const char *contig_file = "tmp/extend_contig.fasta";
        utility::write_fasta("contig", contig_seq, contig_file);

        StringSet<CharString> dropped_read_ids;
        StringSet<Dna5String> dropped_read_seqs;

        vector<shared_ptr<Extension>> tmp_left_extensions;
        vector<shared_ptr<Extension>> tmp_right_extensions;

        vector<bool> realign_reads(length(read_ids), false);
        bool will_realign = false;

        for (auto& ext : left_extensions) {
            if (ext->is_droped) {
                int read_id = ext->read_id();

                if (!realign_reads[read_id]) {
                    realign_reads[read_id] = true;
                    appendValue(dropped_read_ids, read_ids[read_id]);
                    appendValue(dropped_read_seqs, read_seqs[read_id]);
                    will_realign = true;
                }
            } else {
                tmp_left_extensions.emplace_back(ext);
            }
        }

        for (auto& ext : right_extensions) {
            if (ext->is_droped) {
                int read_id = ext->read_id();

                if (!realign_reads[read_id]) {
                    realign_reads[read_id] = true;
                    appendValue(dropped_read_ids, read_ids[read_id]);
                    appendValue(dropped_read_seqs, read_seqs[read_id]);
                    will_realign = true;
                }
            } else {
                tmp_right_extensions.emplace_back(ext);
            }
        }

        if (!will_realign) {
            return contig_seq;
        }

        left_extensions.clear();
        right_extensions.clear();

        left_extensions = tmp_left_extensions;
        right_extensions = tmp_right_extensions;

        const char *reads_file = "tmp/realign_reads.fasta";
        utility::write_fasta(dropped_read_ids, dropped_read_seqs, reads_file);

        aligner::bwa_index(contig_file);

        const char *sam_file = "tmp/realign.sam";
        aligner::bwa_mem(contig_file, reads_file, sam_file);

        BamHeader header;
        vector<BamAlignmentRecord> records;
        utility::read_sam(&header, &records, sam_file);

        find_possible_extensions(records,
                                 &left_extensions,
                                 &right_extensions,
                                 read_name_to_id,
                                 length(contig_seq));

        int real_ext_left = 0;
        for (auto & ext : left_extensions) {
            if (ext->is_droped) {
                real_ext_left++;
            }
        }

        int real_ext_right = 0;
        for (auto & ext : right_extensions) {
            if (ext->is_droped) {
                real_ext_right++;
            }
        }

        std::cout <<  "left ext: " << real_ext_left << " / ";
        std::cout << left_extensions.size() << std::endl;
        std::cout <<  "right ext: " << real_ext_right << " / ";
        std::cout << right_extensions.size() << std::endl;

        if (left_extensions.size() < MIN_COVERAGE &&
            right_extensions.size() < MIN_COVERAGE) {
            return contig_seq;
        }
    }

    return contig_seq;
}

Dna5String extend_contig_poa(const Dna5String& contig_seq,
                    const vector<BamAlignmentRecord>& aln_records,
                    const unordered_map<string, uint32_t>& read_name_to_id) {
    vector<shared_ptr<Extension>> left_extensions;
    vector<shared_ptr<Extension>> right_extensions;

    find_possible_extensions(aln_records,
                         &left_extensions,
                         &right_extensions,
                         read_name_to_id,
                         length(contig_seq));

    vector<string> extensions;
    for (auto& ext : left_extensions) {
        if (!ext->seq().empty()) {
            extensions.emplace_back(ext->seq().substr(0, 1000));
        }
    }
    std::cout << "[INFO] Running left extension poa consensus" << std::endl;
    string left_extension = poa_consensus(extensions);
    reverse(left_extension.begin(), left_extension.end());

    extensions.clear();
    for (auto &ext : right_extensions) {
        if (!ext->seq().empty()) {
            extensions.emplace_back(ext->seq().substr(0, 1000));
        }
    }
    std::cout << "[INFO] Running right extension poa consensus" << std::endl;
    string right_extension = poa_consensus(extensions);

    Dna5String extended_contig = left_extension;
    extended_contig += contig_seq;
    extended_contig += right_extension;
    return extended_contig;
}

}  // namespace scaffolder
