#pragma once
// Game MD5 hashes — backup from strings.h cleanup.
// These are MD5 digests of legitimate/uncracked/untampered game EXEs.
// Will be used in Phase 4 (Patch System) for game identification.

#include "md5.h"

struct djGameMD5 {
    const char* id;
    unsigned char md5[MD5_DIGEST_LENGTH];
};

static struct djGameMD5 djGameMD5s[] = {
    {"ez2dj_1st",       ""},
    {"ez2dj_1st_se",    {'\x57','\x60','\xcf','\xb4','\xf5','\x56','\xd7','\x07','\x11','\xf1','\x8f','\x47','\x34','\x57','\xc5','\x7b'}},
    {"ez2dj_2nd",       ""},
    {"ez2dj_3rd",       {'\xbb','\x44','\x7e','\xe2','\x58','\x1f','\x77','\xd3','\x40','\xd4','\x16','\xd2','\xda','\xf0','\x90','\xab'}},
    {"ez2dj_4th",       {'\xed','\x02','\x84','\x50','\x0b','\x65','\x01','\x9d','\x21','\x95','\xe1','\xa0','\x22','\xa2','\x95','\xde'}},
    {"ez2dj_pt",        {'\xa3','\xe9','\x90','\x89','\x53','\x6e','\x7e','\xea','\xb5','\xe8','\xeb','\x13','\xf9','\x93','\x09','\xcd'}},
    {"ez2dj_6th",       {'\xce','\x6d','\x77','\xd8','\x30','\x36','\x82','\x63','\x6a','\x70','\x50','\xa4','\x32','\x15','\xa1','\x40'}},
    {"ez2dj_7th",       {'\xc2','\x5f','\xd2','\x44','\xff','\x83','\x3f','\x1c','\x28','\x16','\x41','\xda','\x2f','\xf9','\x62','\x67'}},
    {"ez2dj_7th_15",    {'\xc6','\xc7','\xcd','\xc5','\x8b','\x73','\x91','\x6e','\xdf','\xb7','\x17','\xda','\xfb','\xd0','\x41','\x97'}},
    {"ez2dj_7th_20",    {'\x00','\xa6','\x9e','\x80','\xf8','\xbd','\xdf','\xf4','\x8e','\x36','\xb4','\xb4','\x39','\xb9','\x52','\xeb'}},
    {"ez2dj_cv",        {'\x80','\x20','\xb2','\xb1','\x1c','\x93','\x1e','\xfc','\x1f','\xa7','\x7e','\x91','\x7d','\xca','\x18','\xb1'}},
    {"ez2dj_be",        ""},
    {"ez2dj_be_a",      {'\xeb','\xf1','\xcf','\xd5','\x79','\x8a','\x2d','\xb7','\x63','\xce','\xe4','\x76','\x25','\xa8','\xd1','\xe3'}},
    {"ez2dj_ae",        ""},
    {"ez2dj_ae_ic",     ""},
    {"ez2ac_ec",        ""},
    {"ez2ac_ev_w98",    ""},
    {"ez2ac_ev",        {'\x09','\x20','\xe5','\x4a','\x25','\x39','\xe3','\xfd','\x5d','\xe5','\x3a','\xa3','\x7d','\x9b','\x33','\x8a'}},
    {"ez2ac_nt",        {'\x6c','\x12','\x50','\x9f','\x89','\xb3','\x50','\x4c','\x1a','\xab','\xc2','\x9b','\xa7','\xb7','\x32','\xe8'}},
    {"ez2ac_tt",        {'\x03','\x9a','\x5d','\x23','\x3c','\x15','\x12','\x01','\x11','\x2f','\x00','\xfb','\xb6','\x4c','\x21','\xda'}},
    {"ez2ac_fn",        {'\x33','\x6a','\xb9','\x6c','\xae','\x01','\xe0','\x1e','\x06','\x9f','\xb8','\x05','\x58','\x3d','\x02','\x89'}},
    {"ez2ac_fn_ex",     {'\xbc','\xe8','\x48','\xf2','\xd7','\x94','\x5c','\x36','\x12','\x0a','\x2a','\xda','\xa5','\x73','\x57','\x48'}},
};

struct dancerGameMD5 {
    const char* id;
    unsigned char md5[MD5_DIGEST_LENGTH];
};

static struct dancerGameMD5 dancerGameMD5s[] = {
    {"ez2dancer_1st",    ""},
    {"ez2dancer_2nd",    ""},
    {"ez2dancer_uk",     ""},
    {"ez2dancer_uk_se",  ""},
    {"ez2dancer_sc",     ""},
};
