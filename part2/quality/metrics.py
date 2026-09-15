"""按 (规则, 表, 原始行标识) 对账，不用总检出数冒充召回率。"""


def finding_key(item):
    return item["rule"], item["table"], item["row_id"]


def score_findings(expected, actual):
    truth = {finding_key(item) for item in expected}
    found = {finding_key(item) for item in actual}
    result = {}
    for rule in sorted({key[0] for key in truth | found}):
        wanted = {key for key in truth if key[0] == rule}
        observed = {key for key in found if key[0] == rule}
        tp, fp, fn = len(wanted & observed), len(observed-wanted), len(wanted-observed)
        result[rule] = {"truePositive": tp, "falsePositive": fp, "falseNegative": fn,
                        "recall": tp / (tp + fn) if tp + fn else None,
                        "precision": tp / (tp + fp) if tp + fp else None}
    return result
