#!/usr/bin/env python3

import argparse
from pathlib import Path
import subprocess as sp
import csv
import pysam as ps
import random
import numpy as np
import shutil
from sklearn.metrics import log_loss
import json
import urllib.request

try:
    import matplotlib.pyplot as plt
    import seaborn as sns
    import pandas as pd
    plotting_available = True
except ImportError as plot_import_exception:
    plotting_available = False

script_dir = Path(__file__).parent.parent.absolute()
default_octopus_bin = script_dir / 'bin/octopus'

default_germline_measures = "AC AD ADP AF ARF BMQ BQ CC CRF DAD DAF DC DENOVO DP DPC ER ERS FRF GC GQ GQD ITV MC MF MP MRC MQ MQ0 MQD PP PPD QD QUAL REB RSB RTB SB SD SF STRL STRP VL".split()
default_somatic_measures = "AC AD ADP AF ARF BMQ BQ CC CRF DAD DAF DP DPC ER ERS FRF GC GQ GQD ITV NC MC MF MP MRC MQ MQ0 MQD PP PPD QD QUAL REB RSB RTB SB SD SF SHC SMQ SOMATIC STRL STRP VL".split()

known_truth_set_urls = {
    "GIAB": {
        "GRCh37": {
            "NA12878": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_nosomaticdel.bed"
            },
            "HG001": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh37/HG001_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_nosomaticdel.bed"
            },
            "NA24385": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_noinconsistent.bed"
            },
            "HG002": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh37/HG002_GRCh37_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-22_v.3.3.2_highconf_noinconsistent.bed"
            },
            "NA24631": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf_noMetaSV.bed"
            },
            "HG005": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh37/HG005_GRCh37_highconf_CG-IllFB-IllGATKHC-Ion-SOLID_CHROM1-22_v.3.3.2_highconf_noMetaSV.bed"
            }
        },
        "GRCh38": {
            "NA12878": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_nosomaticdel_noCENorHET7.bed"
            },
            "HG001": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_PGandRTGphasetransfer.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/NA12878_HG001/NISTv3.3.2/GRCh38/HG001_GRCh38_GIAB_highconf_CG-IllFB-IllGATKHC-Ion-10X-SOLID_CHROM1-X_v.3.3.2_highconf_nosomaticdel_noCENorHET7.bed"
            },
            "NA24385": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_noinconsistent.bed"
            },
            "HG002": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_triophased.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/AshkenazimTrio/HG002_NA24385_son/NISTv3.3.2/GRCh38/HG002_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf_noinconsistent.bed"
            },
            "NA24631": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.bed"
            },
            "HG005": {
                'vcf': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.vcf.gz",
                'vcf_idx': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.vcf.gz.tbi",
                'bed': "ftp://ftp-trace.ncbi.nlm.nih.gov//giab/ftp/release/ChineseTrio/HG005_NA24631_son/NISTv3.3.2/GRCh38/HG005_GRCh38_GIAB_highconf_CG-Illfb-IllsentieonHC-Ion-10XsentieonHC-SOLIDgatkHC_CHROM1-22_v.3.3.2_highconf.bed"
            }
        }
    }
}

def get_octopus_version(octopus_bin):
    version_line = sp.check_output([str(octopus_bin), '--version']).decode("utf-8").split('\n')[0].split()
    return version_line[2], version_line[-1][:-1] if len(version_line) > 3 else None

def check_exists(paths):
    for path in paths: assert path.exists()

def check_exists_or_none(paths):
    for path in paths: assert path is None or path.exists()

class TrainingData:
    def __init__(self, data):
        self.reference = Path(data["reference"]) if "reference" in data else None
        self.sdf = Path(data["reference_sdf"]) if "reference_sdf" in data else None
        self.reads = data["reads"] if "reads" in data else None
        if type(self.reads) is not list:
            self.reads = [self.reads]
        self.reads = [Path(r) for r in self.reads]
        self.octopus_vcf = Path(data["octopus_vcf"]) if "octopus_vcf" in data else None
        self.regions = Path(data["calling_regions"]) if "calling_regions" in data else None
        self.truth = data["truth"] if "truth" in data else None
        self.confident = Path(data["confident_regions"]) if "confident_regions" in data else None
        self.tp = data["tp_fraction"] if "tp_fraction" in data else 1
        self.fp = data["fp_fraction"] if "fp_fraction" in data else 1
        self.config = Path(data["octopus_config"]) if "octopus_config" in data else None

class TrainingOptions:
    def __init__(self, d):
        self.cross_validation_fraction = d["cross_validation_fraction"] if "cross_validation_fraction" in d else 0.25
        self.hyperparameters = d["hyperparameters"] if "hyperparameters" in d else None

def make_sdf_ref(fasta_ref, rtg, out):
    sp.call([str(rtg), 'format', '-o', str(out), str(fasta_ref)])

def download_truth_set(name, reference, sample, out):
    ftp_vcf = known_truth_set_urls[name][reference][sample]['vcf']
    local_vcf = out / Path(ftp_vcf).name
    urllib.request.urlretrieve(ftp_vcf, local_vcf)
    ftp_vcf_idx = known_truth_set_urls[name][reference][sample]['vcf_idx']
    local_vcf_idx = out / Path(ftp_vcf_idx).name
    urllib.request.urlretrieve(ftp_vcf_idx, local_vcf_idx)
    ftp_bed = known_truth_set_urls[name][reference][sample]['bed']
    local_bed = out / Path(ftp_bed).name
    urllib.request.urlretrieve(ftp_bed, local_bed)
    return local_vcf, local_vcf_idx, local_bed

def load_training_config(options):
    with options.config.open() as config:
        config = json.load(config)

        examples = config['examples'] if "examples" in config else config
        if type(examples) is not list:
            examples = [examples]
        examples = [TrainingData(example) for example in examples]

        assert all(example.reads is not None or example.octopus_vcf is not None for example in examples)
        check_exists([example.reference for example in examples])
        check_exists([example.regions for example in examples])
        check_exists_or_none([reads for example in examples for reads in example.reads])
        check_exists_or_none([example.octopus_vcf for example in examples])
        check_exists_or_none([example.config for example in examples])

        sdf_references, known_truths, given_truths = {}, {}, config["truths"] if "truths" in config else {}

        for label, details in given_truths.items():
            for detail, path in details.items():
                given_truths[label][detail] = Path(path)
            if not details["vcf"].exists():
                raise ValueError(str(details["vcf"]) + " does not exist")
            if not details["bed"].exists():
                raise ValueError(str(details["bed"]) + " does not exist")

        for example in examples:
            if example.sdf is None:
                if example.reference in sdf_references:
                    example.sdf = sdf_references[example.reference]
                else:
                    example.sdf = options.out / (example.reference.stem + '.sdf')
                    make_sdf_ref(example.reference, options.rtg, example.sdf)
                sdf_references[example.reference] = example.sdf
            else:
                if not example.sdf.exists():
                    raise ValueError(example.sdf + " does not exist")

            if type(example.truth) is dict:
                if example.confident is None:
                    example.confident = {}
                for sample, truth_name in example.truth.items():
                    if truth_name in given_truths:
                        example.truth[sample], example.confident[sample] = given_truths[truth_name]['vcf'], given_truths[truth_name]['bed']
                    elif truth_name in known_truths:
                        example.truth[sample], example.confident[sample] = known_truths[example.truth]
                    else:
                        library, reference_version, truth_sample = truth_name.split('//')
                        vcf, _, bed = download_truth_set(library, reference_version, truth_sample, options.out)
                        known_truths[truth_name] = vcf, bed
                        example.truth[sample], example.confident[sample] = vcf, bed
            else:
                if not Path(example.truth).exists():
                    if example.truth in given_truths:
                        example.truth, example.confident = given_truths[example.truth]['vcf'], given_truths[example.truth]['bed']
                    elif example.truth in known_truths:
                        example.truth, example.confident = known_truths[example.truth]
                    else:
                        name, reference_version, sample = example.truth.split('//')
                        vcf, _, bed = download_truth_set(name, reference_version, sample, options.out)
                        known_truths[example.truth] = vcf, bed
                        example.truth, example.confident = vcf, bed
                else:
                    if not example.confident.exists():
                        raise ValueError(example.confident + " does not exist")

        return examples, TrainingOptions(config['training']) if "training" in config else None

def get_reference_id(reference_filename):
    return reference_filename.stem

def get_bam_id(bam_filenames):
    return "_".join(fname.stem for fname in bam_filenames)

def get_octopus_output_filename(reference_filename, bam_filenames, kind="germline"):
    return get_bam_id(bam_filenames) + "." + get_reference_id(reference_filename) + ".Octopus." + kind + ".vcf.gz"

def run_octopus(octopus, reference, reads, regions, threads, output,
                config=None, octopus_vcf=None, kind="germline", annotations="all"):
    octopus_cmd = [str(octopus), '-R', str(reference), '-I'] + \
                  [str(r) for r in reads] + \
                  ['-t', str(regions), \
                   '--ignore-unmapped-contigs', \
                   '--disable-call-filtering', \
                   '--threads', str(threads), \
                   '-o', str(output)]
    if annotations == "all":
        octopus_cmd += ['--annotations', 'all']
    else:
        octopus_cmd += ['--annotations'] + annotations
    if config is not None:
        octopus_cmd += ['--config', str(config)]
    if octopus_vcf is not None:
        octopus_cmd += ['--filter-vcf', str(octopus_vcf)]
    if kind == "somatic":
        octopus_cmd += ['--caller', 'cancer', '--somatics-only']
    sp.call(octopus_cmd)

def read_vcf_samples(vcf_filename):
    vcf = ps.VariantFile(str(vcf_filename))
    return vcf.header.samples

def is_homref(vcf_rec, sample):
    return all(allele == vcf_rec.ref for allele in vcf_rec.samples[sample].alleles)

def has_homref_calls(vcf_filename, sample=None):
    vcf = ps.VariantFile(str(vcf_filename))
    if sample is None:
        assert len(vcf.header.samples) == 1
        sample = vcf.header.samples[0]
    for rec in vcf:
        if is_homref(rec, sample): return True
    return False

def subset_samples(vcf_in_filename, samples, vcf_out_filename=None, drop_uncalled=False, drop_homref=False):
    inplace = False
    if vcf_out_filename is None:
        vcf_out_filename = vcf_in_filename.with_suffix('.gz.tmp')
        inplace = True
    subset_cmd = ['bcftools', 'view', '-s', ','.join(samples), '-Oz', '-o', str(vcf_out_filename)]
    if drop_uncalled:
        subset_cmd.append('-U')
    if drop_homref:
        subset_cmd.append('-c1')
    subset_cmd.append(str(vcf_in_filename))
    sp.call(subset_cmd)
    if inplace:
        shutil.move(str(vcf_out_filename), str(vcf_in_filename))
        index_vcf(vcf_in_filename)
    else:
        index_vcf(vcf_out_filename)

def complement_vcf(src_vcf_filename, tagret_vcf_filenames, dst_vcf_filename):
    sp.call(['bcftools', 'isec', '-C', str(src_vcf_filename)] + [str(f) for f in tagret_vcf_filenames]
            + ['-w1', '-Oz', '-o', str(dst_vcf_filename)])
    index_vcf(dst_vcf_filename)

def intersect_vcfs(src_vcf_filenames, dst_vcf_filename):
    sp.call(['bcftools', 'isec'] + [str(f) for f in src_vcf_filenames]
            + ['-n', str(len(src_vcf_filenames)), '-w1', '-Oz', '-o', str(dst_vcf_filename)])
    index_vcf(dst_vcf_filename)

def concat_vcfs(vcfs, out, remove_duplicates=True):
    assert len(vcfs) > 1
    cmd = ['bcftools', 'concat', '-a', '-Oz', '-o', str(out)]
    if remove_duplicates:
        cmd.append('-D')
    cmd += [str(vcf) for vcf in vcfs]
    sp.call(cmd)
    index_vcf(out)

def remove_vcf_index(vcf_filename):
    vcf_index_filename = vcf_filename.with_suffix(vcf_filename.suffix + '.tbi')
    if vcf_index_filename.exists(): vcf_index_filename.unlink()

def remove_vcf(vcf_filename, remove_index=True):
    vcf_filename.unlink()
    if remove_index: remove_vcf_index(vcf_filename)

def add_vcfeval_missing_homrefs(vcfeval_dir, octopus_vcf, sample=None):
    subsetted = False
    if sample is not None:
        sample_octopus_vcf = vcfeval_dir / (octopus_vcf.stem + '.' + sample + '.vcf.gz')
        subset_samples(octopus_vcf, [sample], sample_octopus_vcf)
        octopus_vcf = sample_octopus_vcf
        subsetted = True

    # Missing TP homref calls should be any calls not in the vcfeval TP or FP sets, but in the source VCF
    vcfeval_tp_vcf_filename, vcfeval_fp_vcf_filename = vcfeval_dir / "tp.vcf.gz", vcfeval_dir / "fp.vcf.gz"
    tp_homref_vcf_filename = vcfeval_dir / 'tp.homref.vcf.gz'
    complement_vcf(octopus_vcf, [vcfeval_tp_vcf_filename, vcfeval_fp_vcf_filename], tp_homref_vcf_filename)
    new_tp_vcf_filename = vcfeval_dir / "tp.with_hom_ref.vcf.gz"
    concat_vcfs([vcfeval_tp_vcf_filename, tp_homref_vcf_filename], new_tp_vcf_filename)
    shutil.move(str(new_tp_vcf_filename), str(vcfeval_tp_vcf_filename))
    remove_vcf_index(new_tp_vcf_filename)
    index_vcf(vcfeval_tp_vcf_filename)
    remove_vcf(tp_homref_vcf_filename)

    # Missing TF homref calls should be any calls in the vcfeval FN and the source set, but not already in the FP set
    fp_homref_vcf_filename = vcfeval_dir / "fp.homref.vcf.gz"
    intersect_vcfs([octopus_vcf, vcfeval_dir / "fn.vcf.gz"], fp_homref_vcf_filename)
    new_fp_vcf_filename = vcfeval_dir / "fp.with_hom_ref.vcf.gz"
    concat_vcfs([vcfeval_fp_vcf_filename, fp_homref_vcf_filename], new_fp_vcf_filename) # removes duplicates already in FP set
    shutil.move(str(new_fp_vcf_filename), str(vcfeval_fp_vcf_filename))
    remove_vcf_index(new_fp_vcf_filename)
    index_vcf(vcfeval_fp_vcf_filename)
    remove_vcf(fp_homref_vcf_filename)

    if subsetted: remove_vcf(octopus_vcf)

def run_vcfeval(rtg, rtg_ref_path, truth_vcf_path, confident_bed_path, octopus_vcf_path, out_dir,
                bed_regions=None, sample=None, kind="germline", include_homref=True):
    cmd = [str(rtg), 'vcfeval', \
           '-t', str(rtg_ref_path), \
           '-b', str(truth_vcf_path), \
           '--evaluation-regions', str(confident_bed_path), \
           '-c', str(octopus_vcf_path), \
           '-o', str(out_dir)]
    if bed_regions is not None:
        cmd += ['--bed-regions', str(bed_regions)]
    if kind == "somatic":
        cmd += ['--squash-ploidy', '--sample', 'ALT']
    elif sample is not None:
        truth_samples = read_vcf_samples(truth_vcf_path)
        if len(truth_samples) > 1:
            raise Exception("More than one sample in truth " + str(truth_vcf_path))
        if sample == truth_samples[0]:
            cmd += ['--sample', sample]
        else:
            cmd += ['--sample', truth_samples[0] + "," + sample]
    sp.call(cmd)
    if sample is not None:
        subset_vcfeval_result_samples(out_dir, sample)
    if include_homref and has_homref_calls(octopus_vcf_path, sample):
        add_vcfeval_missing_homrefs(out_dir, octopus_vcf_path, sample)

def get_annotation(field, rec, sample=None):
    if field == 'QUAL':
        return rec.qual
    elif field in rec.format:
        if sample is None:
            assert len(rec.samples) == 1
            res = rec.samples[list(rec.samples)[0]][field]
        else:
            res = rec.samples[sample][field]
        if type(res) == tuple:
            res = res[0]
        return res
    elif field in rec.info:
        res = rec.info[field]
        if type(res) == tuple:
            res = res[0]
        return res
    else:
        # Field must be a flag and not present
        return 0

def is_somatic_sample(rec, sample):
    return bool(int(get_annotation('SOMATIC', rec, sample)))

def is_somatic_record(rec):
    return any(is_somatic_sample(rec, sample) for sample in list(rec.samples))

def index_vcf(vcf_filename):
    sp.call(['tabix', '-f', str(vcf_filename)])

def filter_somatic(vcf_filename):
    in_vcf = ps.VariantFile(str(vcf_filename))
    tmp_vcf_filename = Path(str(vcf_filename).replace('.vcf', '.tmp.vcf'))
    out_vcf = ps.VariantFile(str(tmp_vcf_filename), 'wz', header=in_vcf.header)
    num_skipped_records = 0
    for rec in in_vcf:
        if is_somatic_record(rec):
            try:
                out_vcf.write(rec)
            except OSError:
                num_skipped_records += 1
    if num_skipped_records > 0:
        print("Skipped " + str(num_skipped_records) + " bad records")
    in_vcf.close()
    out_vcf.close()
    shutil.move(str(tmp_vcf_filename), str(vcf_filename))
    index_vcf(vcf_filename)

def read_octopus_header_info(vcf_filename):
    vcf = ps.VariantFile(str(vcf_filename))
    for record in vcf.header.records:
        if record.key == "octopus":
            return dict(record)
    return None

def startswith_index(options, pattern):
    for i, option in enumerate(options):
        if option.startswith(pattern):
            return i
    return -1

def read_normal_samples(vcf_filename):
    options = read_octopus_header_info(vcf_filename)['options'].split(' ')
    result = []
    for token in options[startswith_index(options, "--normal-sample") + 1:]:
        if token.startswith('--'):
            break
        else:
            result.append(token)
    return result

def is_normal_sample(sample, vcf_filename):
    return sample in read_normal_samples(vcf_filename)

def subset_vcfeval_result_samples(vcfeval_dir, sample):
    subset_samples(vcfeval_dir / 'tp.vcf.gz', [sample])
    subset_samples(vcfeval_dir / 'fp.vcf.gz', [sample])

def read_pedigree(vcf_filename):
    options = read_octopus_header_info(vcf_filename)['options'].split(' ')
    maternal_sample, paternal_sample = None, None
    if '--maternal-sample' in options:
        maternal_sample = options[options.index('--maternal-sample') + 1]
    if '--paternal-sample' in options:
        paternal_sample = options[options.index('--paternal-sample') + 1]
    if maternal_sample is not None and paternal_sample is not None:
        return maternal_sample, paternal_sample
    if '--pedigree' in options:
        return Path(options[options.index('--pedigree') + 1])
    return None

def eval_octopus(octopus, rtg, example, out_dir, threads, kind="germline", measures=None, overwrite=False):
    if example.reads is not None:
        octopus_vcf = out_dir / get_octopus_output_filename(example.reference, example.reads, kind=kind)
        if overwrite or not octopus_vcf.exists():
            run_octopus(octopus, example.reference, example.reads, example.regions, threads, octopus_vcf,
                        config=example.config, octopus_vcf=example.octopus_vcf, kind=kind,
                        annotations="all" if measures is None else measures)
    else:
        assert example.octopus_vcf is not None
        octopus_vcf = example.octopus_vcf
    if kind == "somatic":
        # Hack as '--somatics-only' option is currently ignored when in training mode
        filter_somatic(octopus_vcf)
    samples = read_vcf_samples(octopus_vcf)
    result = []
    if len(samples) == 1:
        vcfeval_dir = out_dir / (octopus_vcf.stem + '.eval')
        if not vcfeval_dir.exists() or overwrite:
            if vcfeval_dir.exists(): shutil.rmtree(vcfeval_dir)
            run_vcfeval(rtg, example.sdf, example.truth, example.confident, octopus_vcf, vcfeval_dir, bed_regions=example.regions, kind=kind)
        result.append(vcfeval_dir)
    else:
        for sample in samples:
            if sample in example.truth:
                vcfeval_dir = out_dir / (octopus_vcf.stem + '.' + sample + '.eval')
                if not vcfeval_dir.exists() or overwrite:
                    if vcfeval_dir.exists(): shutil.rmtree(vcfeval_dir)
                    sample_kind = kind
                    if kind == "somatic" and is_normal_sample(sample, octopus_vcf):
                        sample_kind = "germline"
                    run_vcfeval(rtg, example.sdf, example.truth[sample], example.confident[sample], octopus_vcf, vcfeval_dir,
                                bed_regions=example.regions, sample=sample, kind=sample_kind)
                result.append(vcfeval_dir)
    return result

def subset(vcf_in_path, vcf_out_path, bed_regions):
    sp.call(['bcftools', 'view', '-R', str(bed_regions), '-O', 'z', '-o', str(vcf_out_path), str(vcf_in_path)])

def is_missing(x):
    return x is None or x == '.' or np.isnan(float(x))

def to_str(x):
    return str(x) if type(x) != bool else str(int(x))

def annotation_to_string(x, missing_value):
    return to_str(missing_value) if is_missing(x) else to_str(x)

def make_ranger_data(octopus_vcf_filename, out_path, classifcation, measures, missing_value=-1, fraction=1):
    vcf = ps.VariantFile(str(octopus_vcf_filename))
    with out_path.open(mode='w') as ranger_data:
        datawriter = csv.writer(ranger_data, delimiter=' ')
        for rec in vcf:
            if fraction >= 1 or random.random() <= fraction:
                row = [annotation_to_string(get_annotation(measure, rec), missing_value) for measure in measures]
                row.append(str(int(classifcation)))
                datawriter.writerow(row)

def concat(filenames, outpath):
    with outpath.open(mode='w') as outfile:
        for fname in filenames:
            with fname.open() as infile:
                for line in infile:
                    outfile.write(line)

def shuffle(fname):
    lines = fname.open().readlines()
    random.shuffle(lines)
    fname.open(mode='w').writelines(lines)

def add_header(fname, header):
    lines = fname.open().readlines()
    with fname.open(mode='w') as f:
        f.write(header + '\n')
        f.writelines(lines)

def partition_data(data_fname, validation_fraction, training_fname, validation_fname):
    with data_fname.open() as data_file:
        header = data_file.readline()
        with training_fname.open(mode='w') as training_file, validation_fname.open(mode='w') as validation_file:
            training_file.write(header)
            validation_file.write(header)
            for example in data_file:
                if random.random() < validation_fraction:
                    validation_file.write(example)
                else:
                    training_file.write(example)

def run_ranger_training(ranger, data_path, hyperparameters, threads, out, seed=None):
    cmd = [str(ranger), '--file', str(data_path), '--depvarname', 'TP', '--probability',
           '--nthreads', str(threads), '--outprefix', str(out), '--write', '--impmeasure', '1', '--verbose']
    if 'trees' in hyperparameters:
        cmd += ['--ntree', str(hyperparameters["trees"])]
    if 'min_node_size' in hyperparameters:
        cmd += ['--targetpartitionsize', str(hyperparameters["min_node_size"])]
    if 'max_depth' in hyperparameters:
        cmd += ['--maxdepth', str(hyperparameters["max_depth"])]
    if seed is not None:
        cmd += ['--seed', str(seed)]
    sp.call(cmd)

def run_ranger_prediction(ranger, forest, data_path, threads, out):
    cmd = [str(ranger), '--file', str(data_path), '--predict', str(forest),
           '--probability', '--nthreads', str(threads), '--outprefix', str(out), '--verbose']
    sp.call(cmd)

def read_predictions(prediction_filename):
    with prediction_filename.open() as prediction_file:
        next(prediction_file)
        next(prediction_file)
        next(prediction_file)
        return np.array([float(line.strip().split()[0]) for line in prediction_file])

def read_truth_labels(truth_filename):
    with truth_filename.open() as truth_file:
        true_label_index = truth_file.readline().strip().split().index('TP')
        return np.array([int(line.strip().split()[true_label_index]) for line in truth_file])

def select_training_hypterparameters(master_data_filename, training_params, options):
    if training_params is None or training_params.hyperparameters is None:
        return {"trees": 500, "min_node_size": 10}
    elif len(training_params.hyperparameters) == 1:
        return training_params.hyperparameters[0]
    else:
        # Use cross-validation to select hypterparameters
        training_data_filename = Path(str(master_data_filename).replace('.dat', '.train.data'))
        validation_data_filename = Path(str(master_data_filename).replace('.dat', '.validate.data'))
        partition_data(master_data_filename, training_params.cross_validation_fraction, training_data_filename, validation_data_filename)
        optimal_params, min_loss = None, None
        cross_validation_temp_dir = options.out / 'cross_validation'
        cross_validation_temp_dir.mkdir(parents=True, exist_ok=True)
        cross_validation_prefix = cross_validation_temp_dir / options.prefix
        prediction_filename = cross_validation_prefix.with_suffix(cross_validation_prefix.suffix + '.prediction')
        forest_filename = cross_validation_prefix.with_suffix(cross_validation_prefix.suffix + '.forest')
        for hyperparameters in training_params.hyperparameters:
            print('Training cross validation forest with hyperparameters', hyperparameters)
            run_ranger_training(options.ranger, training_data_filename, hyperparameters, options.threads, cross_validation_prefix, seed=10)
            run_ranger_prediction(options.ranger, forest_filename, validation_data_filename, options.threads, cross_validation_prefix)
            truth_labels, predictions = read_truth_labels(validation_data_filename), read_predictions(prediction_filename)
            loss = log_loss(truth_labels, predictions)
            print('Binary cross entropy =', loss)
            if min_loss is None or loss < min_loss:
                min_loss, optimal_params = loss, hyperparameters 
        shutil.rmtree(cross_validation_temp_dir)
        return optimal_params

def main(options):
    if options.kind not in ["germline", "somatic"]:
        print('kind must be "germline" or "somatic"')
        exit(1)
    examples, training_params = load_training_config(options)
    options.out.mkdir(parents=True, exist_ok=True)
    data_files, tmp_files = [], []
    default_measures = default_germline_measures if options.kind == "germline" else default_somatic_measures
    for example in examples:
        vcfeval_dirs = eval_octopus(options.octopus, options.rtg, example, options.out, options.threads, kind=options.kind, measures=default_measures, overwrite=options.overwrite)
        for vcfeval_dir in vcfeval_dirs:
            tp_vcf_path = vcfeval_dir / "tp.vcf.gz"
            tp_train_vcf_path = Path(str(tp_vcf_path).replace("tp.vcf", "tp.train.vcf"))
            if not tp_train_vcf_path.exists(): subset(tp_vcf_path, tp_train_vcf_path, example.regions)
            tp_data_path = Path(str(tp_train_vcf_path).replace(".vcf.gz", ".dat"))

            if not tp_data_path.exists(): make_ranger_data(tp_train_vcf_path, tp_data_path, True, default_measures, options.missing_value, fraction=example.tp)
            data_files.append(tp_data_path)
            fp_vcf_path = vcfeval_dir / "fp.vcf.gz"
            fp_train_vcf_path = Path(str(fp_vcf_path).replace("fp.vcf", "fp.train.vcf"))
            if not fp_train_vcf_path.exists(): subset(fp_vcf_path, fp_train_vcf_path, example.regions)
            fp_data_path = Path(str(fp_train_vcf_path).replace(".vcf.gz", ".dat"))
            if not fp_data_path.exists(): make_ranger_data(fp_train_vcf_path, fp_data_path, False, default_measures, options.missing_value, fraction=example.fp)
            data_files.append(fp_data_path)
            tmp_files += [tp_train_vcf_path, fp_train_vcf_path]
    master_data_file = options.out / (str(options.prefix) + ".dat")
    concat(data_files, master_data_file)
    if not options.keep_example_data_files:
        for file in tmp_files + data_files:
            file.unlink()
    shuffle(master_data_file)
    ranger_header = ' '.join(default_measures + ['TP'])
    add_header(master_data_file, ranger_header)
    ranger_out_prefix = options.out / options.prefix
    hyperparameters = select_training_hypterparameters(master_data_file, training_params, options)
    run_ranger_training(options.ranger, master_data_file, hyperparameters, options.threads, ranger_out_prefix)

    if plotting_available:
        importance_filename = ranger_out_prefix.with_suffix(ranger_out_prefix.suffix + ".importance")
        with importance_filename.open() as f:
            importances = dict(t.strip().split(':') for t in f.readlines())
            for measure, importance in importances.items():
                importances[measure] = float(importance)
            importances = pd.DataFrame.from_dict(importances, orient='index', columns=['Importance']).reset_index().rename(columns={'index': 'Measure'})
            important, not_important = importances.query('Importance > 0'), importances.query('Importance == 0')

            g = sns.barplot(x='Measure', y='Importance',
                            data=important)
            for item in g.get_xticklabels():
                item.set_rotation(45)
                g.tick_params(labelsize=5)
            plt.savefig(importance_filename.with_suffix(".important.pdf"), format='pdf', transparent=True, bbox_inches='tight')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--config',
                        required=True,
                        type=Path,
                        help='Training data config in json format')
    parser.add_argument('--octopus',
                        type=Path,
                        default=default_octopus_bin,
                        help='Octopus binary')
    parser.add_argument('--rtg',
                        type=Path,
                        default='rtg',
                        help='RTG Tools binary')
    parser.add_argument('--ranger',
                        type=Path,
                        default='ranger',
                        help='Ranger binary')
    parser.add_argument('-o', '--out',
                        type=Path,
                        required=True,
                        help='Output directory')
    parser.add_argument('--prefix',
                        type=Path,
                        default='octopus',
                        help='Output files prefix')
    parser.add_argument('-t', '--threads',
                        type=int,
                        default=1,
                        help='Number of threads for octopus')
    parser.add_argument('--missing-value',
                        type=float,
                        default=-1,
                        help='Value for missing measures')
    parser.add_argument('--kind',
                        type=str,
                        default='germline',
                        help='Kind of random forest to train [germline, somatic]')
    parser.add_argument('--overwrite',
                        default=False,
                        help='Overwrite existing calls and evaluation files',
                        action='store_true')
    parser.add_argument('--keep-example-data-files',
                        default=False,
                        help='Do not delete generated training data files for each example',
                        action='store_true')
    parsed, unparsed = parser.parse_known_args()
    if len(unparsed) == 0:
        main(parsed)
    else:
        print("Error: unparsed options", unparsed)
